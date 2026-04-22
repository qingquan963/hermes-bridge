#include "ExecHandler.h"
#include "Logger.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

static std::string to_string(const std::wstring& w) {
    if (w.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), NULL, 0, NULL, NULL);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &result[0], size, NULL, NULL);
    return result;
}

// Get absolute path to PowerShell using GetSystemDirectoryW
static std::wstring getPowerShellPath() {
    wchar_t sysDir[MAX_PATH];
    if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0) {
        return L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    return std::wstring(sysDir) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}

// Get absolute path to cmd.exe using GetSystemDirectoryW
static std::wstring getCmdPath() {
    wchar_t sysDir[MAX_PATH];
    if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0) {
        return L"C:\\Windows\\System32\\cmd.exe";
    }
    return std::wstring(sysDir) + L"\\cmd.exe";
}

HandlerResult ExecHandler::handle(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string command = params.value("command", "");
    std::string shell = params.value("shell", "powershell");
    std::string cwd = params.value("cwd", "");
    int timeout = params.value("timeout", ctx.default_timeout);

    if (command.empty()) {
        return HandlerResult::errorResult("INVALID_REQUEST", "command is required", "", 0);
    }

    // P0-1: Validate command for dangerous characters to prevent RCE
    const char* dangerous = ";|&$`()<>\\/";
    for (const char* p = dangerous; *p; ++p) {
        if (command.find(*p) != std::string::npos) {
            std::ostringstream oss;
            oss << "EXEC_FAILED: forbidden character '" << *p << "' in command";
            return HandlerResult::errorResult("EXEC_FAILED", oss.str(), "", 0);
        }
    }
    // Also reject newlines / null bytes
    if (command.find('\n') != std::string::npos || command.find('\r') != std::string::npos ||
        command.find('\0') != std::string::npos) {
        return HandlerResult::errorResult("EXEC_FAILED", "EXEC_FAILED: forbidden whitespace character in command", "", 0);
    }

    // Build command line using CreateProcessW lpCommandLine (not shell)
    // This avoids shell interpretation of special characters
    std::wstring wcmd = std::wstring(command.begin(), command.end());
    std::wstring wshell_exe;
    std::wstring wshell_args;

    if (shell == "powershell") {
        wshell_exe = getPowerShellPath();
        wshell_args = L"-NoProfile -NonInteractive -Command \"" + wcmd + L"\"";
    } else if (shell == "cmd") {
        wshell_exe = getCmdPath();
        wshell_args = L"/C " + wcmd;
    } else if (shell == "python") {
        wshell_exe = L"python.exe";
        wshell_args = L"-c " + wcmd;
    } else {
        // shell == "none": execute command directly as a program name
        // For safety, only allow if it looks like a simple filename
        // Reject if it contains path separators
        if (command.find('\\') != std::string::npos || command.find('/') != std::string::npos) {
            return HandlerResult::errorResult("EXEC_FAILED", "EXEC_FAILED: path separators not allowed in direct mode", "", 0);
        }
        wshell_exe = wcmd;
        wshell_args = L"";
    }

    // Setup pipes for stdout/stderr
    HANDLE hStdOutRd = NULL, hStdOutWr = NULL;
    HANDLE hStdErrRd = NULL, hStdErrWr = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0);
    CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0);

    STARTUPINFOW si = {sizeof(STARTUPINFOW)};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdErrWr;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = {};
    std::wstring wdir = cwd.empty() ? L"" : std::wstring(cwd.begin(), cwd.end());

    // Use lpApplicationName (executable) and lpCommandLine (args) separately
    // to avoid shell interpretation of dangerous characters
    LPWSTR cmdLine = wshell_args.empty() ? NULL : &wshell_args[0];
    BOOL created = CreateProcessW(
        wshell_exe.empty() ? NULL : wshell_exe.c_str(),
        cmdLine,
        NULL, NULL, TRUE,
        CREATE_NO_WINDOW, NULL,
        wdir.empty() ? NULL : wdir.c_str(),
        &si, &pi);

    if (!created) {
        CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
        CloseHandle(hStdErrRd); CloseHandle(hStdErrWr);
        DWORD err = GetLastError();
        std::ostringstream oss;
        oss << "CreateProcess failed with code " << err;
        return HandlerResult::errorResult("EXEC_FAILED", oss.str(), "", 0);
    }

    // Close write ends in parent
    CloseHandle(hStdOutWr);
    CloseHandle(hStdErrWr);

    // Wait with timeout
    DWORD waitResult = WaitForSingleObject(pi.hProcess, timeout * 1000);
    DWORD exitCode = 0;
    bool killed = false;

    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        exitCode = 1;
        killed = true;
        LOG_WARN("Exec timeout, killed process: " + command);
    } else {
        GetExitCodeProcess(pi.hProcess, &exitCode);
    }

    // Read stdout
    std::string stdout_str, stderr_str;
    {
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hStdOutRd, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
            stdout_str.append(buf, bytesRead);
        }
        CloseHandle(hStdOutRd);
    }
    {
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hStdErrRd, buf, sizeof(buf), &bytesRead, NULL) && bytesRead > 0) {
            stderr_str.append(buf, bytesRead);
        }
        CloseHandle(hStdErrRd);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["stdout"] = stdout_str;
    res["stderr"] = stderr_str;
    res["exit_code"] = exitCode;
    res["killed"] = killed;

    return HandlerResult::okResult(std::move(res), duration_ms);
}
