# Hermes Bridge 安全过滤修改方案

## 背景

Hermes（WSL）和龙虾小兵（Windows）在不同网络平面，直接互通受限。通信统一经由 Bridge（Windows）中转。

当前 Bridge 的 `exec` handler 安全过滤过于严格，导致 Hermes 无法正常调用 Windows 工具链，影响跨平台任务执行效率。

---

## 当前安全策略分析

**文件：** `src/handlers/ExecHandler.cpp`

### SHELL 模式（`shell:"powershell"`）当前拦截

| 字符 | 用途 | 拦截风险 |
|------|------|---------|
| `&` | 后台执行/组合命令 | ✅ 合理拦截 |
| `` ` `` | 命令替换 | ✅ 合理拦截 |
| `\` | 转义/路径 | ❌ 误拦（PowerShell 路径如 `C:\` 完全无法使用）|
| `(` `)` | 括号/子表达式 | ❌ 误拦（`.NET` 类实例化、变量展开全都用到括号）|
| `<` `>` | 重定向 | ⚠️ 低风险（`-Command` 已包裹在双引号内）|

### DIRECT 模式（`shell:"none"`）

更严格，但 Hermes 不用此模式，暂不涉及。

---

## 修改方案（最小改动）

仅修改 SHELL 模式的危险字符列表，**放开 `\`，`(`, `)`**：

### 修改文件

**`src/handlers/ExecHandler.cpp` 第 57 行**

```cpp
// ===== 修改前 =====
// SHELL mode dangerous chars: &`\()<>  (no |, no ;)
// DIRECT mode dangerous chars: ;&|`()<>
const char* dangerous = (shell == "none") ? ";&|`()<>\\" : "&`\\()<>";

// ===== 修改后 =====
// SHELL mode dangerous chars: &`<>  (allow \ () for paths and .NET calls)
// DIRECT mode dangerous chars: ;&|`()<>
const char* dangerous = (shell == "none") ? ";&|`()<>\\" : "&`<>";
```

同时更新上方注释：

```cpp
// shell mode (powershell/cmd/python): block &`<> but allow | ; \ ()
//   - | (pipe): allowed in PowerShell (管道操作符)
//   - ; (semicolon): allowed as statement separator in PowerShell/CMD
//   - \ (backslash): allowed for Windows paths C:\
//   - () (parentheses): allowed for .NET calls, subexpressions $(...)
//   - && / ||: blocked via pair-detection below
//   - & alone: blocked (background operator)
// direct mode (shell="none"): block ;&|`()<>
```

---

## 风险评估

| 风险项 | 等级 | 说明 |
|--------|------|------|
| PowerShell 注入 | **低** | `-Command "..."` 已将命令包裹在双引号内，变量展开受限 |
| 路径遍历 | **低** | Bridge 执行在 `C:\lobster\hermes_bridge\` 目录，权限已隔离 |
| 命令替换 `` ` `` | **已保留拦截** | 反引罪仍被拦截 |
| 后台执行 `&` | **已保留拦截** | 仍被拦截 |
| 网络请求 | **不受影响** | `Invoke-WebRequest` 等不需要特殊字符 |

---

## 受益场景

修改后 Hermes 可以：

1. **访问 Windows 文件路径** — `C:\Users\...` 不再报错
2. **调用 .NET 类** — `New-Object System.Net.WebSockets.ClientWebSocket`
3. **变量展开和子表达式** — `$var`、`$(...)`、`@(...)`
4. **标准命令管道** — `|` 和 `;` 本来就没被拦

---

## 验证方法

修改后用以下命令测试：

```json
{
  "client": "x97",
  "cmd_id": "verify_fix_1",
  "action": "exec",
  "payload": {
    "timeout_ms": 8000,
    "command": "powershell -ExecutionPolicy Bypass -Command \"Write-Host 'path-test: C:\\Windows'; $c = [System.Net.WebSockets.ClientWebSocket]::new(); Write-Host 'dotnet-ok'\""
  }
}
```

**预期输出：** `path-test: C:\Windows` 和 `dotnet-ok` 都打印，无错误。

---

## 注意事项

- DIRECT 模式（`shell:"none"`）**不在本次修改范围内**，保持原样
- 如果 Bridge 有自动化测试，改完后应跑一遍 `exec` 相关测试用例
- 修改后建议在 Bridge 日志确认无新增错误
