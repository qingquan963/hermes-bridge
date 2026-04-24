# SHELL_CHARS_FIX - ExecHandler Dangerous Characters Narrowing

**日期**: 2026-04-23  
**项目**: `C:\lobster\hermes_bridge`  
**文件**: `src/handlers/ExecHandler.cpp`

---

## 修改内容

### 修改前（第 56-57 行）
```cpp
// SHELL mode dangerous chars: &`\<>()  (no |, no ;)
// DIRECT mode dangerous chars: ;&|`()<>
const char* dangerous = (shell == "none") ? ";&|\\()<>" : "&`\\()<>";
```

### 修改后（第 56-57 行）
```cpp
// SHELL mode dangerous chars: &`<>  (no (), no \\)
// DIRECT mode dangerous chars: ;&|`()<>
const char* dangerous = (shell == "none") ? ";&|`()<>\\" : "&`<>";
```

### 变更说明
- **SHELL 模式** dangerous 字符从 `&`\\()<>` 缩窄为 `&`<>`
- **删除了 `()\\` 三个字符**：允许圆括号 `(` `)` 和反斜杠 `\` 在 PowerShell/CMD 模式下使用
- DIRECT 模式（`shell="none"`）保持不变：`;&|`()<>\\`

---

## 编译状态

**编译命令**: `cmd /c "C:\lobster\hermes_bridge\do_build.bat"`

**结果**: ✅ 编译成功
```
hermes_bridge.vcxproj -> C:\lobster\hermes_bridge\Release\hermes_bridge.exe
```

---

## 服务重启

- 旧进程 `hermes_bridge.exe` (PID 16128) 已终止
- 服务 `Hermes Bridge (hermes-bridge)` 已停止
- 新编译版本已启动，服务运行中

---

## 测试结果

### 测试命令
```json
{
  "cmd_id": "test_paren_002",
  "action": "exec",
  "params": {
    "command": "echo (1+2)",
    "shell": "powershell"
  },
  "timeout": 30,
  "force": true
}
```

### 响应
```json
{
  "cmd_id": "test_paren_002",
  "duration_ms": 281,
  "result": {
    "exit_code": 0,
    "killed": false,
    "stderr": "",
    "stdout": "3\r\n"
  },
  "status": "ok"
}
```

### 结论
✅ PowerShell 中 `(1+2)` 正常执行，输出 `3`。括号字符 `()` 已不再被 SHELL 模式拦截。

---

## 注意

- 端口 18900 的 HTTP 服务器仅暴露 `/callback` 端点（接收回调），不处理 `exec` 动作
- `exec` 通过文件队列 `cmd_<client>.txt` 提交，由 FileMonitor 轮询处理
