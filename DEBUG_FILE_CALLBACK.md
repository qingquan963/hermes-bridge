# DEBUG_FILE_CALLBACK.md — file:// 回调崩溃根因诊断

**项目路径**：`C:\lobster\hermes_bridge\`
**诊断日期**：2026-04-23
**诊断人**：Debugger Subagent

---

## 一、问题现象

- 测试用例 `cb3` 使用 `callback_url = "file://C:/lobster/hermes_bridge/cb3_result.txt"`
- 进程在命令入队后崩溃（无完成日志），约 7 分钟后自动重启
- SSRF 检查 (`isBlockedUrl`) 预期应阻止 `file://` 协议，但进程仍崩溃
- 日志中未出现 `SSRF blocked` 警告（对比 `cb2` 测试有该日志）

---

## 二、代码分析

### 2.1 `src/CallbackClient.cpp` 关键逻辑

#### SSRF 检查 `isBlockedUrl`
```cpp
bool isBlockedUrl(const std::string& url) {
    if (url.size() < 7) return true;
    std::string lower = url;
    for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
    if (lower.rfind("http://", 0) != 0 && lower.rfind("https://", 0) != 0)
        return true;
    // ... 后续主机名/IP 检查
}
```
- 长度检查：`file://` 长度恰好为 7，通过
- 协议检查：`file://` 不以 `http://` 或 `https://` 开头 → 返回 `true`（被阻止）
- **结论**：`isBlockedUrl("file://...")` 应返回 `true`

#### `asyncCallback` 流程
```cpp
void asyncCallback(const std::string& url, ...) {
    if (isBlockedUrl(url)) {
        LOG_WARN("[{}] [callback] SSRF blocked: url={}", client_id, url);
        return;  // 直接返回，不创建线程
    }
    // 以下为 WinHTTP 处理流程（仅当 URL 通过 SSRF 检查时执行）
    // ...
}
```
- 若 `isBlockedUrl` 返回 `true`，应记录警告并立即返回
- 日志中无该警告，说明可能：
    1. `isBlockedUrl` 返回 `false`（错误地放行）
    2. `asyncCallback` 未被调用（`callback_url` 为空）
    3. 进程在记录日志前崩溃

### 2.2 `src/ExecHandler.cpp` 危险字符检查
- 危险字符黑名单：`;&|`()<>\\`（不含 `file://`）
- `file://` 不被视为命令注入危险字符，与回调崩溃无关

### 2.3 文件解析流程 (`FileMonitor.cpp`)
- `callback_url` 字段解析：`cmd.cmd_callback_url = cmd_json.value("callback_url", "");`
- 若 JSON 中无 `callback_url` 字段，则值为空字符串
- `asyncCallback` 仅在 `!cmd.cmd_callback_url.empty()` 时调用

---

## 三、日志分析

### 3.1 `logs/events.txt` 关键片段
```
[2026-04-23 05:18:33.790] [INFO] [cb3] Enqueued cmd cb-test-003 (action=exec, force=0)
...（之后无完成日志）...
[2026-04-23 05:25:52.837] [INFO] Hermes Bridge starting...   # 进程重启
```
- `cb3` 入队后无 `Completed` 日志，进程在约 7 分钟后重启 → 崩溃发生在命令处理期间
- 无 `SSRF blocked` 日志 → `isBlockedUrl` 可能未触发

### 3.2 对比测试 `cb2`
```
[2026-04-23 05:18:18.843] [WARN] [cb2] [callback] SSRF blocked: url=http://127.0.0.1:8007/health
[2026-04-23 05:18:18.843] [INFO] [cb2] Completed cmd cb-test-002 (status=ok, duration_ms=83)
```
- `cb2` 使用 `http://127.0.0.1`，正确触发 SSRF 阻止并记录警告
- 说明 SSRF 检查功能正常，日志系统正常

---

## 四、崩溃根因推断

### 可能性 1：`isBlockedUrl` 错误放行 `file://`
- 经手动模拟（PowerShell 复现），`isBlockedUrl("file://...")` 返回 `true`
- 但代码中可能存在边界情况未覆盖：
    - URL 长度恰好为 7 时 (`"file://"`)，`url.size() < 7` 为 `false`，通过
    - 协议检查使用 `rfind`，逻辑正确
- **低概率**，但需确认实际二进制中的代码是否与源码一致

### 可能性 2：`asyncCallback` 未被调用（`callback_url` 为空）
- 若测试用例中 `callback_url` 字段缺失或为空，则不会进入回调逻辑
- 但进程仍崩溃，说明崩溃点可能在命令执行本身（`ExecHandler`）
- 然而 `cb3` 的 action 为 `exec`，与 `cb1`/`cb2` 相同，不应单独崩溃

### 可能性 3：崩溃发生在 `isBlockedUrl` 之前
- `asyncCallback` 首行即调用 `isBlockedUrl`，之前仅有参数传递
- 参数为字符串，传递过程不会导致崩溃

### 可能性 4：崩溃发生在 `isBlockedUrl` 内部
- 函数内无动态内存分配，仅循环和字符串操作
- 若 `url` 包含非法字符或编码问题，`tolower` 可能引发未定义行为
- **低概率**

### 可能性 5：`WinHttpCrackUrl` 处理 `file://` 时崩溃
- 若 `isBlockedUrl` 错误返回 `false`，代码将执行到 `WinHttpCrackUrl`
- `WinHttpCrackUrl` 设计用于 HTTP/HTTPS URL，对 `file://` 可能产生未定义行为（访问违规）
- **最可能**：SSRF 检查因未知原因放行，WinHTTP 尝试解析 `file://` 导致进程崩溃

---

## 五、触发条件

1. **URL 格式**：`file://` 后跟本地路径（如 `file://C:/path/to/file.txt`）
2. **SSRF 检查**：必须错误地返回 `false`（原因待查）
3. **WinHTTP 调用**：`WinHttpCrackUrl` 尝试解析非 HTTP 协议 URL

---

## 六、建议修复方案

### 短期（立即修复）
1. **在 `isBlockedUrl` 中显式拒绝 `file://` 协议**：
   ```cpp
   if (lower.rfind("file://", 0) == 0) return true;
   ```
2. **增加更多调试日志**：
   - 在 `asyncCallback` 入口记录 URL
   - 记录 `isBlockedUrl` 的返回值
   - 记录 `WinHttpCrackUrl` 的调用结果
3. **确保 `isBlockedUrl` 在 `asyncCallback` 开始时被调用**（已满足）

### 长期（架构加固）
1. **URL 协议白名单**：仅允许 `http://` 和 `https://`，其他一律拒绝
2. **异常处理**：在 `WinHttpCrackUrl` 调用处添加 `__try`/`__except` 防止进程崩溃
3. **单元测试**：增加对 `file://`、`ftp://`、`gopher://` 等协议的 SSRF 测试用例

---

## 七、验证方法

1. 应用修复后，重新执行 `cb3` 测试
2. 预期行为：
   - `isBlockedUrl` 返回 `true`
   - 日志中出现 `SSRF blocked` 警告
   - 进程正常完成命令，无崩溃
3. 若仍崩溃，则需进一步排查 WinHTTP 初始化或线程创建环节

---

## 八、附件

- `src/CallbackClient.cpp` 相关代码片段（见上文）
- `logs/events.txt` 关键行（见上文）
- 手动测试脚本 `test_isBlocked.ps1`（位于工作目录）

---

**诊断结论**：最可能的崩溃根因为 `isBlockedUrl` 未能正确阻止 `file://` 协议，导致 `WinHttpCrackUrl` 处理非 HTTP URL 时引发访问违规。建议立即添加 `file://` 显式拒绝并增加调试日志。

**下一步**：Executor 根据本报告实施修复，验证后更新 `REVIEW_CALLBACK.md`。
