#include "ProcessHandler.h"
#include "Logger.h"
#include <windows.h>
#include <tlhelp32.h>
#include <chrono>
#include <vector>
#include <psapi.h>
#include <nlohmann/json.hpp>

static std::vector<DWORD> getPidsByName(const std::string& name) {
    std::vector<DWORD> pids;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return pids;

    PROCESSENTRY32W pe = {sizeof(PROCESSENTRY32W)};
    std::wstring wname(name.begin(), name.end());

    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (std::wstring(pe.szExeFile) == wname) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return pids;
}

static std::vector<DWORD> getPidsByPort(int port) {
    std::vector<DWORD> pids;
    // Use netstat to find pid by port
    STARTUPINFOW si = {sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"netsh.exe diag show vai Port_Connection=" + std::to_wstring(port);
    // Simpler: parse tasklist
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    CloseHandle(hSnap);
    // For now, return empty (would need to parse netstat output)
    return pids;
}

HandlerResult ProcessHandler::handle(const HandlerContext& ctx) {
    std::string action = ctx.cmd.value("action", "");
    if (action == "process_start") return handleStart(ctx);
    if (action == "process_stop") return handleStop(ctx);
    return HandlerResult::errorResult("INVALID_REQUEST", "Unknown process action", "", 0);
}

HandlerResult ProcessHandler::handleStart(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string command = params.value("command", "");
    bool detached = params.value("detached", true);
    std::string cwd = params.value("cwd", "");

    if (command.empty()) return HandlerResult::errorResult("INVALID_REQUEST", "command is required", "", 0);

    std::wstring wcmd(command.begin(), command.end());
    std::wstring wdir = cwd.empty() ? L"" : std::wstring(cwd.begin(), cwd.end());

    STARTUPINFOW si = {sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION pi = {};
    DWORD flags = CREATE_NO_WINDOW;
    if (detached) flags |= DETACHED_PROCESS;

    BOOL created = CreateProcessW(NULL, &wcmd[0], NULL, NULL, FALSE, flags,
                                   NULL, wdir.empty() ? NULL : wdir.c_str(), &si, &pi);

    if (!created) {
        return HandlerResult::errorResult("EXEC_FAILED", "CreateProcess failed for: " + command,
                                          "err=" + std::to_string(GetLastError()), 0);
    }

    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    if (detached) {
        CloseHandle(pi.hProcess); // Don't wait for detached process
    } else {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
    }

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["pid"] = pid;
    res["command"] = command;
    return HandlerResult::okResult(std::move(res), duration_ms);
}

HandlerResult ProcessHandler::handleStop(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string name = params.value("name", "");
    int pid = params.value("pid", 0);
    int port = params.value("port", 0);

    std::vector<DWORD> pids_to_kill;

    if (pid > 0) {
        pids_to_kill.push_back(static_cast<DWORD>(pid));
    } else if (!name.empty()) {
        pids_to_kill = getPidsByName(name);
    } else if (port > 0) {
        pids_to_kill = getPidsByPort(port);
    } else {
        return HandlerResult::errorResult("INVALID_REQUEST", "name, pid or port is required", "", 0);
    }

    int killed_count = 0;
    for (DWORD p : pids_to_kill) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, p);
        if (h) {
            if (TerminateProcess(h, 1)) ++killed_count;
            CloseHandle(h);
        }
    }

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["stopped"] = killed_count > 0;
    res["killed_pids"] = pids_to_kill;
    return HandlerResult::okResult(std::move(res), duration_ms);
}
