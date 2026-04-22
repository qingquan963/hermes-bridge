#include "ServiceHandler.h"
#include "Logger.h"
#include <windows.h>
#include <winsvc.h>
#include <chrono>

static std::string getServiceStatusStr(DWORD state) {
    switch (state) {
        case SERVICE_STOPPED: return "Stopped";
        case SERVICE_START_PENDING: return "StartPending";
        case SERVICE_STOP_PENDING: return "StopPending";
        case SERVICE_RUNNING: return "Running";
        case SERVICE_CONTINUE_PENDING: return "ContinuePending";
        case SERVICE_PAUSE_PENDING: return "PausePending";
        case SERVICE_PAUSED: return "Paused";
        default: return "Unknown";
    }
}

HandlerResult ServiceHandler::handle(const HandlerContext& ctx) {
    return handleQuery(ctx);
}

HandlerResult ServiceHandler::handleQuery(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();
    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string service_name = params.value("service_name", "");

    if (service_name.empty()) {
        return HandlerResult::errorResult("INVALID_REQUEST", "service_name is required", "", 0);
    }

    std::wstring wname(service_name.begin(), service_name.end());

    SC_HANDLE hSCM = OpenSCManagerW(NULL, NULL, GENERIC_READ);
    if (!hSCM) {
        return HandlerResult::errorResult("INTERNAL_ERROR", "OpenSCManager failed",
                                          "err=" + std::to_string(GetLastError()), 0);
    }

    SC_HANDLE hSvc = OpenServiceW(hSCM, wname.c_str(), GENERIC_READ);
    if (!hSvc) {
        CloseServiceHandle(hSCM);
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_DOES_NOT_EXIST) {
            return HandlerResult::errorResult("SERVICE_NOT_FOUND", "Service not found: " + service_name, "", 0);
        }
        return HandlerResult::errorResult("INTERNAL_ERROR", "OpenService failed",
                                          "err=" + std::to_string(err), 0);
    }

    // Get status via SERVICE_STATUS_PROCESS
    BYTE status_buf[sizeof(SERVICE_STATUS_PROCESS) + 256];
    DWORD bytes_needed = 0;
    SERVICE_STATUS_PROCESS* ssp = reinterpret_cast<SERVICE_STATUS_PROCESS*>(status_buf);
    if (!QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, status_buf, sizeof(status_buf), &bytes_needed)) {
        CloseServiceHandle(hSvc);
        CloseServiceHandle(hSCM);
        return HandlerResult::errorResult("INTERNAL_ERROR",
            "QueryServiceStatusEx failed", "err=" + std::to_string(GetLastError()), 0);
    }

    // Get start type via QueryServiceConfigW
    BYTE config_buf[4096];
    if (!QueryServiceConfigW(hSvc, reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config_buf), sizeof(config_buf), &bytes_needed)) {
        CloseServiceHandle(hSvc);
        CloseServiceHandle(hSCM);
        return HandlerResult::errorResult("INTERNAL_ERROR",
            "QueryServiceConfigW failed", "err=" + std::to_string(GetLastError()), 0);
    }
    QUERY_SERVICE_CONFIGW* qsc = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config_buf);

    // Get display name
    std::wstring display_buf(256, L'\0');
    DWORD bufSize = 0;
    GetServiceDisplayNameW(hSCM, wname.c_str(), &display_buf[0], &bufSize);
    std::wstring display_name(display_buf.begin(), display_buf.begin() + bufSize);

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hSCM);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["name"] = service_name;
    res["display_name"] = std::string(display_name.begin(), display_name.end());
    res["status"] = getServiceStatusStr(ssp->dwCurrentState);
    res["start_type"] = (qsc->dwStartType == SERVICE_AUTO_START) ? "Automatic" :
                        (qsc->dwStartType == SERVICE_DEMAND_START) ? "Manual" : "Disabled";
    res["can_stop"] = ((ssp->dwControlsAccepted & SERVICE_ACCEPT_STOP) != 0);

    return HandlerResult::okResult(std::move(res), duration_ms);
}
