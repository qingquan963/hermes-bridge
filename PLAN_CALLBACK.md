# Hermes Bridge — 回调模式执行计划

**版本**：v1.0
**日期**：2026-04-23
**制定人**：Planner
**审核人**：Architect（龙虾小兵）
**状态**：待审核
**类型**：P1.3（增量功能，不改现有逻辑）

---

## 一、背景与目标

Hermes Bridge 当前通过轮询 `out_*.txt` 文件获取结果，最坏延迟 = 5s poll + 执行时间。

**目标**：命令执行完成后，Bridge 主动 POST 结果到指定 URL，消除轮询延迟。

**性质**：纯增量开发，不修改现有逻辑，不影响老命令行为。

---

## 二、技术决策

### 2.1 HTTP 客户端：✅ WinHTTP（已有 P1.1 实现）

> PLAN.md 原提 libcurl，实际 HttpHandler 已用 WinHTTP 实现，统一复用。

| 方案 | 决策 | 理由 |
|------|------|------|
| WinHTTP（复用 HttpHandler） | ✅ 采用 | P1.1 已实现 http_post，代码成熟，超时机制完善 |
| libcurl | ❌ 不采用 | 引入新依赖，PLAN.md 表述与实际实现不符 |

### 2.2 回调触发位置：ThreadPool Worker 内

回调在 `Worker 执行完 handler → writeResult 之后` 触发。最小改动点，不引入新线程/异步框架。

### 2.3 回调超时：5s，独立于命令执行超时

```
命令执行超时：由各 Handler 自己控制（exec 默认 30s）
回调超时：固定 5s，超时视为失败，记录 warn 日志
```

---

## 三、任务拆解

### P1.3 回调模式（1 人天）

#### P1.3.1 cmd JSON 新增 callback_url 字段支持

```
[P1.3.1.1] 在 FileMonitor 或 Config 中，确保 cmd JSON 可以解析 callback_url 字段
[P1.3.1.2] 确认 callback_url 为可选字段，缺失或 null 均视为无回调
[P1.3.1.3] callback_url 只允许 http://（暂不支持 https 验证，与 http_get/http_post 一致）
```

**验收**：
- [ ] cmd 带 `callback_url: "http://127.0.0.1:9000/callback"` 时可正常解析
- [ ] cmd 无 callback_url 字段时行为与修改前完全一致
- [ ] callback_url 为 `null` 或空字符串时视为无回调

---

#### P1.3.2 回调触发逻辑（ThreadPool Worker 内）

```
[P1.3.2.1] 在 ThreadPool worker 执行完 handler.handle() 并 writeResult 之后
           增加回调判断逻辑：
           if (!callback_url.empty()) {
               triggerCallback(callback_url, result_json);
           }
[P1.3.2.2] triggerCallback() 为独立函数，内部创建临时 HttpPostRequest
           - POST body = result_json.dump(-1, ' ', false)（与 out 文件内容完全一致）
           - Content-Type: application/json
           - 超时固定 5s
           - 回调线程：std::thread（detach），不阻塞 worker
[P1.3.2.3] 回调触发后不等待响应，不改变命令执行结果
```

**验收**：
- [ ] callback_url 存在时，执行完 POST 一次
- [ ] callback_url 为空/null 时，行为与现在完全一致（不回调）
- [ ] POST 超时（如 5s）不阻塞 Bridge 主流程
- [ ] 回调 POST 成功/失败都有对应日志

---

#### P1.3.3 回调日志

```
[P1.3.3.1] 回调发起时记录 info 日志：
           [info] [worker-N] [client_id] Callback triggered: POST <url>
[P1.3.3.2] 回调成功（收到 HTTP 响应）记录 info 日志：
           [info] [worker-N] [client_id] Callback succeeded: status_code=<N>
[P1.3.3.3] 回调失败（网络错误/超时）记录 warn 日志：
           [warn] [worker-N] [client_id] Callback failed: <error_msg>
           （不记录 stack trace，不影响主流程）
```

**验收**：
- [ ] events.txt 包含上述三种日志关键字
- [ ] 回调失败后 Bridge 进程依然正常运行，state.json 保持 running

---

## 四、验收标准（可测量）

> 每条标准均可通过脚本或人工验证，不依赖主观判断。

| # | 验收标准 | 验证方法 |
|---|---------|---------|
| **V1** | callback_url 存在时，执行完 POST 一次 | 启动本地 HTTP 服务器（nc -l 9000），发一条带 callback_url 的 cmd，验证收到一次 POST |
| **V2** | callback_url 为空/null 时，行为与现在完全一致（不回调） | 发一条不带 callback_url 的 cmd，Bridge 行为与修改前一致（out 文件正常写入，无回调日志） |
| **V3** | POST 超时（如 5s）不阻塞 Bridge 主流程 | callback_url 指向不存在的地址（10.255.255.1:9999），Bridge 5s 后恢复正常响应，不卡死 |
| **V4** | 回调 POST 成功有 info 日志 | 查看 events.txt，grep "Callback succeeded" |
| **V5** | 回调 POST 失败有 warn 日志，不影响主流程 | callback_url 指向无效地址，events.txt 有 "Callback failed"，Bridge 继续正常运行 |

---

## 五、技术难点识别

| 难点 | 描述 | 应对措施 |
|------|------|---------|
| **M1：回调时机** | 必须在 writeResult 完成后触发，确保 out 文件已就绪 | 在 ThreadPool worker 的 run() 末尾，在 writeResult 之后触发回调 |
| **M2：非阻塞** | 回调 HTTP 请求不能阻塞 worker，否则影响其他命令执行 | `std::thread(&triggerCallback, ...).detach()`，独立线程发 POST |
| **M3：POST body 与 out 文件一致** | 回调 body 必须与 out_*.txt 内容完全一致 | 直接使用 writeResult 写入前的 `result_json`，不做二次序列化 |
| **M4：向后兼容** | 老命令不带 callback_url 必须不受影响 | `callback_url.empty()` 判断，为空则跳过回调逻辑 |

---

## 六、依赖关系

```
P1.3 依赖 P0（已完成）+ P1.1（HttpHandler 已实现）
    │
    ├── P1.3.1 cmd JSON 解析（无额外依赖）
    ├── P1.3.2 回调触发逻辑（复用 HttpHandler::handlePost 底层 WinHTTP 调用）
    └── P1.3.3 日志（复用 spdlog）
    │
    ↓
V1–V5 验收
```

---

## 七、工时估算

| 子任务 | 预估人天 | 累计 |
|--------|---------|------|
| P1.3.1 callback_url 字段支持 | 0.1 | 0.1 |
| P1.3.2 回调触发逻辑（独立线程 + WinHTTP POST） | 0.6 | 0.7 |
| P1.3.3 日志 | 0.1 | 0.8 |
| V1–V5 验收测试 | 0.2 | 1.0 |
| **合计** | | **1 人天** |

---

## 八、文件变更清单

```
C:\lobster\hermes_bridge\src\
    ThreadPool.cpp          # 修改：worker 末尾增加回调触发逻辑
    ThreadPool.h            # 修改：增加 triggerCallback 声明
    CallbackHandler.cpp     # 新增：回调逻辑（独立 .cpp）
    CallbackHandler.h       # 新增：回调逻辑头文件
```

**不修改的文件**（向后兼容）：
- 所有 handlers（ExecHandler、FileHandler、HttpHandler、OllamaHandler 等）
- FileMonitor、ResultWriter、Config 等核心组件

---

## 九、后续动作

1. **本计划提交 Architect 审核**
2. **审核通过后**：Executor 领取 P1.3 任务
3. **完成后**：验收 V1–V5（可本地用 Python http.server 或 nc 模拟回调服务器）
4. **上线**：功能开关通过 callback_url 字段控制，无 URL 则完全不影响现有逻辑

---

*Planner 制定 | 待龙虾小兵审核*
