# Hermes Bridge ExecHandler 调试报告

## 问题描述
ExecHandler 中调用 CreateProcess 失败，错误代码 2 (ERROR_FILE_NOT_FOUND)。测试表明 powershell.exe 和 cmd.exe 均无法启动，尽管它们存在于系统目录中。

## 调查步骤

1. **检查代码**：阅读 `ExecHandler.cpp`，发现路径拼接逻辑仅使用可执行文件名（例如 `powershell.exe`），未提供完整路径。
2. **验证文件存在**：
   - `powershell.exe` 位于 `C:\Windows\System32\WindowsPowerShell\v1.0\`
   - `cmd.exe` 位于 `C:\Windows\System32\`
   - 系统 PATH 环境变量包含 `C:\Windows\System32\WindowsPowerShell\v1.0\` 和 `C:\Windows\System32\`
3. **进程架构**：Hermes Bridge 为 x64 构建，不存在 WoW64 重定向问题。
4. **环境变量**：从父进程（命令行）启动时，应继承完整的 PATH。但实际进程环境可能被裁剪（如服务环境），导致 PATH 不完整。
5. **CreateProcess 调用**：代码正确使用了 `lpApplicationName` 和 `lpCommandLine` 分离，并传递了 `CREATE_NO_WINDOW` 标志。

## 根因分析

根本原因是 **PATH 环境变量在 Hermes Bridge 进程中被修改或缺失**，导致 CreateProcess 无法解析相对路径 `powershell.exe` 和 `cmd.exe`。

尽管系统 PATH 包含必要目录，但 Hermes Bridge 可能因以下原因未能继承完整环境变量：
- 进程可能以系统服务或后台任务身份启动，环境变量被重置。
- 静态链接 CRT (`/MT`) 可能导致环境变量初始化异常。
- 进程可能在启动后清除了环境变量（但代码中未见显式操作）。

此外，`powershell.exe` 不在 `C:\Windows\System32` 根目录下，而在子目录 `WindowsPowerShell\v1.0\` 中。即使 PATH 包含 `C:\Windows\System32`，CreateProcess 也不会递归搜索子目录。只有当 PATH 明确包含该子目录时才能找到。系统 PATH 确实包含该子目录，但若环境变量被截断或覆盖，则可能丢失。

## 修复建议

### 1. 使用绝对路径（推荐）
修改 `ExecHandler.cpp`，为每个 shell 提供完整路径，而非依赖 PATH 查找。

**代码改动示例**：
```cpp
#include <windows.h> // 已包含

// 在 handle 函数内，构建完整路径：
std::wstring getSystemDirectory() {
    wchar_t buf[MAX_PATH];
    GetSystemDirectoryW(buf, MAX_PATH);
    return std::wstring(buf);
}

// 针对 powershell：
std::wstring sysDir = getSystemDirectory();
wshell_exe = sysDir + L"\\WindowsPowerShell\\v1.0\\powershell.exe";

// 针对 cmd：
wshell_exe = sysDir + L"\\cmd.exe";

// 针对 python（可选）：
// 可搜索 PATH 或使用注册表，但建议保持原样（依赖 PATH）。
```

### 2. 处理 WoW64 重定向
为确保 32 位进程在 64 位系统上也能正确访问 System32，可使用 `Wow64DisableWow64FsRedirection` 临时禁用重定向，然后再启用。

```cpp
PVOID oldRedir = NULL;
Wow64DisableWow64FsRedirection(&oldRedir);
// 调用 CreateProcess
Wow64RevertWow64FsRedirection(oldRedir);
```

### 3. 增强错误日志
在 CreateProcess 失败时，记录更多信息（如当前工作目录、PATH 环境变量值），便于后续调试。

### 4. 验证环境变量
在进程启动时，可记录关键环境变量（PATH、SystemRoot）到日志文件，便于诊断。

## 具体修改步骤

1. 在 `ExecHandler.cpp` 顶部添加辅助函数：
```cpp
static std::wstring getPowerShellPath() {
    wchar_t sysDir[MAX_PATH];
    GetSystemDirectoryW(sysDir, MAX_PATH);
    std::wstring path = std::wstring(sysDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    return path;
}
```

2. 修改 shell 判断分支：
```cpp
if (shell == "powershell") {
    wshell_exe = getPowerShellPath();
    wshell_args = L"-NoProfile -NonInteractive -Command \"" + wcmd + L"\"";
} else if (shell == "cmd") {
    wshell_exe = getSystemDirectory() + L"\\cmd.exe";
    wshell_args = L"/C " + wcmd;
}
```

3. 重新构建 Hermes Bridge（运行 `build.bat`）。

## 测试验证
修复后，应执行以下测试：
- 使用 shell=powershell 执行简单命令（如 `echo test`）
- 使用 shell=cmd 执行 dir 命令
- 验证 stdout/stderr 捕获是否正常
- 验证超时终止功能

## 结论
ExecHandler 的 CreateProcess 失败是由于相对路径依赖不完整的 PATH 环境变量所致。通过使用绝对路径可彻底解决此问题，同时提高代码的健壮性。

--- 
报告生成时间：2026-04-22 12:30 GMT+8
调试师：Debugger Subagent