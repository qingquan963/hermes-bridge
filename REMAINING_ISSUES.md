# Hermes Bridge — 遗留问题修复需求

**日期**: 2026-04-23  
**提出人**: 百事通 (Hermes Agent)  
**优先级**: P0 = 必须修复 / P1 = 应该修复 / P2 = 最好修复

---

## P0-1：进程稳定性 — NSSM 自动拉起

### 问题描述
Bridge 进程会随机僵住或崩溃退出，没有任何自动恢复机制。

### 复现步骤
1. 启动 Bridge (PID 8232)
2. 持续发送 5-6 个请求
3. 约 5-10 分钟后进程无响应，轮询停止
4. 日志文件时间戳停在某个时间点，state.json 不更新

### 期望行为
进程崩溃或僵住后，5 秒内自动拉起，继续服务。

### 解决方案
注册 NSSM Windows Service，参考 PLAN.md Section 2.1 的命令：

```powershell
nssm install HermesBridge C:\lobster\hermes_bridge\Release\hermes_bridge.exe ""
nssm set HermesBridge AppDirectory C:\lobster\hermes_bridge\Release
nssm set HermesBridge Description "Hermes Bridge - Hermes Agent Windows Resource Bridge"
nssm set HermesBridge Start SERVICE_AUTO_START
nssm set HermesBridge AppRestartDelay 5000
net start HermesBridge
```

### 验收标准
- [ ] `sc query HermesBridge` 显示 RUNNING
- [ ] Kill 进程后 5s 内自动重启
- [ ] 重启后 state.json 的 pid 更新，uptime_seconds 重置

---

## P0-2：file:// 回调导致进程崩溃

### 问题描述
使用 `file://` 协议的 callback_url 时，进程直接崩溃退出。

### 复现步骤
```json
{
  "cmd_id": "cb-test-003",
  "action": "exec",
  "params": {"command": "powershell -Command \"Get-Date\"", "shell": "powershell"},
  "callback_url": "file://C:/lobster/hermes_bridge/cb3_result.txt"
}
```
发送后进程僵住，最终退出。

### 期望行为
- file:// 回调正常执行，将结果写入指定文件
- 或明确返回 error，不崩溃

### 验收标准
- [ ] `file://C:/path/to/file` 回调写入文件成功
- [ ] 进程不崩溃
- [ ] events.txt 有正确的 callback 日志

---

## P1-1：安全检查过于严格，误杀正常 PowerShell 命令

### 问题描述
安全检查拦截了 `|` (pipe) 和 `;` (semicolon) 字符，导致大部分 PowerShell 命令无法执行。

### 复现场景
```json
{"action": "exec", "params": {"command": "powershell -Command \"Get-Date | Select-Object -Property DateTime\""}}
```
返回: `EXEC_FAILED: forbidden character '|' in command`

### 影响
- 所有带管道操作的 PowerShell 命令被拦截
- 链式命令（`cmd1; cmd2`）被拦截
- 严重影响 Bridge 的实用性

### 期望行为
安全检查只拦截真正危险的字符/模式，例如：
- `&` (命令链接) — 可以考虑放行
- `&&` / `||` — 高危，应拦截
- `|` (管道) — PowerShell 管道是合法操作，应放行
- `;` (语句分隔) — 需要评估，大多数场景应放行

### 建议方案
1. 将安全检查从"字符黑名单"改为"模式白名单"
2. 对于 `shell=powershell`，允许管道操作
3. 允许 `&` 在某些场景下使用

### 验收标准
- [ ] `powershell -Command "Get-Process | Select-Object -First 5"` 执行成功
- [ ] `powershell -Command "Write-Output 'a'; Write-Output 'b'"` 执行成功
- [ ] `& notepad.exe` 仍被拦截（危险命令）
- [ ] `&& notepad.exe && del C:\Windows\` 仍被拦截（命令链危险）

---

## P1-2：state.json 统计数据不准确

### 问题描述
stats 字段的数字与实际不符。

### 复现场景
实际处理了 6 个请求（4 ok + 2 error），但 state.json 显示：
```
total_requests: 2
ok_requests: 2
error_requests: 0
```

### 期望行为
stats 准确反映累计处理数量。

### 验收标准
- [ ] 每处理一个请求 total_requests +1
- [ ] 成功时 ok_requests +1
- [ ] 失败时 error_requests +1
- [ ] 重启后 stats 重置为 0

---

## P2-1：进程僵死时无告警

### 问题描述
进程僵住时没有任何告警机制，用户不知道服务已不可用。

### 期望行为
guardian 或外部监控检测到进程无响应时发送告警。

### 解决方案（可选）
- 进程僵住检测：events.txt 超过 60s 无新日志
- 告警方式：写告警文件或发 webhook

### 验收标准
- [ ] events.txt 超过 60s 无更新时，触发告警机制

---

## 文件清理（后续需要）

测试过程中产生的临时文件，需要清理：
- `C:\lobster\hermes_bridge\cmd_test*.txt`
- `C:\lobster\hermes_bridge\out_test*.txt`
- `C:\lobster\hermes_bridge\cmd_cb*.txt`
- `C:\lobster\hermes_bridge\out_cb*.txt`
- `C:\lobster\hermes_bridge\cb3_result.txt`
- `C:\lobster\hermes_bridge\cmd_hermes.txt`
- `C:\lobster\hermes_bridge\out_hermes.txt`
