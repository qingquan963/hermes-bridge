# Hermes Bridge 复测报告 V2

**测试时间**: 2026-04-22 12:47 (GMT+8)
**测试工程师**: QA Subagent
**Bridge PID**: 16840
**Bridge 版本**: 1.0.0
**测试对象**: hermes_bridge.exe (2026-04-22 12:42:48)

---

## 一、修复验证摘要

| Bug | 描述 | 修复方案 | 验证结果 |
|-----|------|----------|----------|
| Bug #1 | exec action CreateProcess code 2 | PowerShell 绝对路径 `GetSystemDirectoryW` | ✅ PASS |
| Bug #2 | client_id 显示 `main.txt\0` | 正确剥离 `.txt` 后缀和尾部空白字符 | ✅ PASS |
| Bug #3 | ok_requests 下溢 (18446744073709551611) | 条件表达式 `errors>=total_requests?0:total_requests-errors` | ✅ PASS |
| Bug #4 | 日志 action 为空 `(action=)` | 预先捕获 `action_for_log` 再 enqueue | ✅ PASS |

---

## 二、详细测试结果

### Test 1: Bug #1 - exec action (PowerShell)

**测试命令** (`cmd_main.txt`):
```json
{"action":"exec","params":{"command":"echo hello from powershell","shell":"powershell"},"cmd_id":"exec-test-001"}
```

**结果文件** (`out_main.txt`):
```json
{
  "cmd_id":"exec-test-001",
  "duration_ms":270,
  "result":{
    "exit_code":0,
    "killed":false,
    "stderr":"",
    "stdout":"hello\r\nfrom\r\npowershell\r\n"
  },
  "status":"ok"
}
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| CreateProcess 成功 | exit_code=0 | exit_code=0 | ✅ PASS |
| stdout 有输出 | "hello\r\nfrom\r\npowershell\r\n" | 完全匹配 | ✅ PASS |
| stderr 为空 | "" | "" | ✅ PASS |
| 无错误码2 | 不再出现 ERROR_FILE_NOT_FOUND | 未出现 | ✅ PASS |

**日志片段**:
```
[2026-04-22 12:47:12.309] [INFO] [main] Enqueued cmd exec-test-001 (action=exec)
[2026-04-22 12:47:12.587] [INFO] [main] Completed cmd exec-test-001 (status=ok, duration_ms=270)
```

---

### Test 2: Bug #2 - client_id 格式

**state.json 快照** (3个不同 client):
```json
{
  "clients": ["main", "test2", "test3"],
  "stats": {
    "total_requests": 3,
    "ok_requests": 3,
    "error_requests": 0
  }
}
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| client_id 无 ".txt" 后缀 | "main" | "main" | ✅ PASS |
| 无 `\0` 结尾字符 | 无 null char | 无 null char | ✅ PASS |
| 多 client 正确识别 | ["main","test2","test3"] | 完全匹配 | ✅ PASS |

---

### Test 3: Bug #3 - ok_requests 计数器

**state.json stats** (3次成功请求后):
```json
"stats": {
  "total_requests": 3,
  "ok_requests": 3,
  "error_requests": 0
}
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| ok_requests 无下溢 | 3 | 3 | ✅ PASS |
| total_requests 正确 | 3 | 3 | ✅ PASS |
| error_requests 正确 | 0 | 0 | ✅ PASS |
| 数学关系正确 | ok=total-error=3 | 3=3-0 | ✅ PASS |

**对比上一轮**:
- 上一轮: `ok_requests: 18446744073709551611` (uint64 下溢)
- 本轮: `ok_requests: 3` (正确)

---

### Test 4: Bug #4 - 日志 action 字段

**events.txt 日志片段**:
```
[2026-04-22 12:47:12.309] [INFO] [main] Enqueued cmd exec-test-001 (action=exec)
[2026-04-22 12:47:47.393] [INFO] [test2] Enqueued cmd hg-v2-001 (action=http_get)
[2026-04-22 12:47:47.394] [INFO] [test3] Enqueued cmd fr-v2-001 (action=file_read)
[2026-04-22 12:47:47.406] [INFO] [test3] Completed cmd fr-v2-001 (status=ok, duration_ms=0)
[2026-04-22 12:47:48.645] [INFO] [test2] Completed cmd hg-v2-001 (status=ok, duration_ms=1242)
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| Enqueued 日志 action 非空 | (action=exec) | (action=exec) | ✅ PASS |
| http_get action 正确 | (action=http_get) | (action=http_get) | ✅ PASS |
| file_read action 正确 | (action=file_read) | (action=file_read) | ✅ PASS |
| Completed 日志存在 | status=ok | status=ok | ✅ PASS |

---

## 三、额外功能验证

### Test 5: http_get action

**测试命令** (`cmd_test2.txt`):
```json
{"action":"http_get","params":{"url":"https://httpbin.org/get"},"cmd_id":"hg-v2-001"}
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| status_code | 200 | 200 | ✅ PASS |
| duration_ms | <5000 | 1242ms | ✅ PASS |
| stdout 包含 origin | 有 | 有 | ✅ PASS |

### Test 6: file_read action

**测试命令** (`cmd_test3.txt`):
```json
{"action":"file_read","params":{"path":"C:\\lobster\\hermes_bridge\\test_read.txt"},"cmd_id":"fr-v2-001"}
```

| 检查项 | 预期 | 实际 | 状态 |
|--------|------|------|------|
| status | ok | ok | ✅ PASS |
| duration_ms | <1000 | 0ms | ✅ PASS |

---

## 四、最终验收

| Bug ID | 描述 | 状态 |
|--------|------|------|
| Bug #1 | exec action CreateProcess code 2 | ✅ **FIXED** |
| Bug #2 | client_id 显示 `main.txt\0` | ✅ **FIXED** |
| Bug #3 | ok_requests 下溢 | ✅ **FIXED** |
| Bug #4 | 日志 action 为空 | ✅ **FIXED** |

**复测结论**: 全部 4 个 Bug 均已修复，核心功能验证通过。

---

*测试报告由 QA Subagent 生成 | 2026-04-22*
