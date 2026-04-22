# Hermes Bridge — 回调模式架构设计

**文档版本**：v1
**更新日期**：2026-04-23
**架构师**：Architect
**状态**：✅ 已实现（修复版）

---

## 一、总体架构描述

在现有文件轮询架构基础上，新增可选回调机制：cmd JSON 支持 `callback_url` 字段，命令执行完毕后 Bridge 主动以 HTTP POST 推送完整结果 JSON 到该 URL。回调通过独立后台线程异步发起，不阻塞 worker，不影响文件写入主流程，向后兼容无 `callback_url` 的指令。

---

## 二、回调流程

```
cmd JSON 入队
    ↓
FileMonitor.scanAndEnqueue() 解析 JSON，提取 callback_url 写入 Command.cmd_callback_url
    ↓
ThreadPool worker 取指令 → runCommandHandler(cmd)
    ↓
Handler 执行（ExecHandler 等）
    ↓
runCommandHandler() 构造 resp JSON（cmd_id / status / result|error / duration_ms）
    ↓
g_writer->writeResult(client_id, resp)   ← 文件写入（原子，不变）
    ↓
【异步回调节点】如果 cmd.callback_url 非空：
    spawn detached thread → asyncCallback(cmd.callback_url, resp)
    ↓
worker 立即返回，不等待 POST 结果
```

---

## 三、关键设计决策

### 3.1 回调时机

**决策**：回调在 `g_writer->writeResult()` 之后发起。

**理由**：
- 保证文件结果已落盘，回调失败不影响 Hermes 读取
- 与现有 ResultWriter 正交，互不侵入
- 若回调在写文件之前发起且失败，Hermes 读不到结果（更严重）

### 3.2 异步 POST

**决策**：独立 `std::thread` 发起 POST，主 worker 不等待、不阻塞。

**理由**：同步 POST 会占用 worker 线程；若回调 URL 慢或超时，整体吞吐腰斩。异步化后 worker 可立即处理下一条指令。

**实现**：`std::thread([url, body]() { HttpPostAsync(url, body); }).detach()`，注意 detach 后线程生命周期独立于主线程。

### 3.3 超时策略

**决策**：超时 **5 秒**，超时后 warn 日志，**不重试，不阻塞**。

| 场景 | 处理 |
|------|------|
| 连接超时（网络不可达）| warn 日志，忽略 |
| HTTP 4xx | warn 日志（含状态码），忽略 |
| HTTP 5xx | warn 日志，忽略 |
| 超时（5s）| warn 日志，忽略 |
| 成功（2xx）| debug 日志，忽略 |

**理由**：回调是"尽最大努力"通知，重试会增加复杂度且可能引发幂等性问题。 caller 侧若需要可靠送达，应在 caller 侧做补偿。

### 3.4 向后兼容

**决策**：`callback_url` 完全可选。

- cmd JSON 无此字段 → 指令正常执行、正常写文件，**完全不触发回调**
- FileMonitor 解析时若字段缺失，Command.cmd_callback_url 为空串
- 回调触发前检查 `!cmd.callback_url.empty()`，空则跳过

### 3.5 POST Body

**决策**：直接使用完整 `resp` JSON（即写入 out 文件的同一 JSON），Content-Type 为 `application/json`。

```json
{
  "cmd_id": "uuid-xxx",
  "status": "ok",
  "result": { ... },
  "duration_ms": 123
}
```

---

## 四、数据流

### 4.1 callback_url 读取位置

```
FileMonitor::scanAndEnqueue()
  → nlohmann::json cmds = json::parse(content)
  → for each cmd_json:
      cmd.cmd_callback_url = cmd_json.value("callback_url", "")
```

### 4.2 结果构造位置

```
runCommandHandler(cmd)
  → HandlerResult result = handler->handle(ctx)
  → json resp; resp["cmd_id"]=...; resp["status"]=...; resp["result"]=...
  → g_writer->writeResult(cmd.client_id, resp)
```

### 4.3 POST 发送位置

```
runCommandHandler(cmd) 内，g_writer->writeResult() 之后：
  → if (!cmd.callback_url.empty()) {
        std::thread([cmd_url=cmd.callback_url, body=resp.dump()]() {
            HttpPostAsync(cmd_url, body);
        }).detach();
    }
```

### 4.4 HTTP Client 选型

**决策**：复用现有 libcurl（已静态链接），新增 `CallbackHttpClient` 类封装 POST 逻辑。

现有 `HttpHandler` 使用 WinHTTP。考虑到 libcurl 已在 DESIGN.md 中确认静态链接且为推荐方案，**回调 HTTP client 新增 `libcurl_async.cpp/.h`**，与 `HttpHandler`（WinHTTP）解耦。

---

## 五、日志规格

| 事件 | 级别 | 格式 |
|------|------|------|
| 回调触发 | DEBUG | `[client_id] Callback triggered for cmd cmd_id → url` |
| 回调成功（2xx）| DEBUG | `[client_id] Callback succeeded for cmd cmd_id (HTTP xxx, took Xms)` |
| 回调失败（连接超时）| WARN | `[client_id] Callback failed for cmd cmd_id: connection timeout (url=...)` |
| 回调失败（HTTP 4xx/5xx）| WARN | `[client_id] Callback failed for cmd cmd_id: HTTP xxx (url=...)` |
| 回调失败（其他异常）| WARN | `[client_id] Callback failed for cmd cmd_id: exception=...` |
| 回调跳过（无 URL）| DEBUG | `[client_id] No callback_url for cmd cmd_id, skipping` |

---

## 六、代码改动清单

### 6.1 新增文件

| 文件 | 职责 |
|------|------|
| `src/CallbackClient.cpp` | 封装 libcurl 异步 POST，超时 5s |
| `src/CallbackClient.h` | 头文件 |

### 6.2 修改文件

| 文件 | 改动 |
|------|------|
| `src/CommandQueue.h` | `Command` 结构体新增 `std::string cmd_callback_url` |
| `src/main.cpp` | `runCommandHandler()` 内，writeResult 后发起异步回调 |
| `src/FileMonitor.cpp` | `scanAndEnqueue()` 解析 `callback_url` 字段 |

---

## 七、潜在风险

| 风险 | 等级 | 缓解措施 |
|------|------|----------|
| **回调线程泄漏**：detached thread 若崩溃无人知晓 | 低 | catch all exception，写 warn 日志 |
| **cmd.callback_url 为恶意 URL**（SSRF）| 中 | 验收文档说明 caller 职责，Bridge 不做 URL 白名单 |
| **POST body 含敏感信息**（暴露在网络）| 中 | caller 应自行评估，建议内网 URL，文档提示 |
| **高频指令触发大量回调线程** | 低 | 独立线程池上限（建议 max 10 并发回调线程），超出则 queue 等待 |
| **libcurl 和 WinHTTP 共存冲突** | 低 | libcurl 静态链接，WinHTTP 在 HttpHandler，两者独立 |
| **回调写入在文件写入之前崩溃** | 低 | 回调在文件写入之后发起，此风险已消除 |

---

## 八、向后兼容性验证

| 场景 | 预期行为 |
|------|----------|
| 旧 cmd JSON（无 callback_url） | 完全不变，执行、写文件正常，无回调 |
| 新 cmd JSON（有 callback_url） | 正常执行、写文件，回调 POST 异步发出 |
| callback_url 为空字符串 `""` | 等同于无 callback_url，跳过回调 |
| callback_url 格式非法 | warn 日志，回调失败，不影响主流程 |
