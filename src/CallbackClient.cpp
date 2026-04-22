#include "CallbackClient.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <thread>
#include <cstring>
#include <atomic>
#include <mutex>
#include <condition_variable>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

namespace {
    // ── Thread semaphore: max 10 concurrent callbacks ────────────────────────
    std::atomic<int>         g_active_callbacks{0};
    std::mutex               g_sem_mutex;
    std::condition_variable  g_sem_cv;
    constexpr int MAX_CONCURRENT_CALLBACKS = 10;

    // ── SSRF protection ───────────────────────────────────────────────────────
    bool isBlockedUrl(const std::string& url, const std::string& client_id, const std::string& cmd_id) {
        if (url.size() < 7) {
            LOG_WARN("[callback] SSRF blocked: url too short (client={}, cmd_id={})", client_id, cmd_id);
            return true;
        }
        std::string lower = url;
        for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);

        // Explicitly reject file:// protocol (prevents WinHTTP crash)
        if (lower.rfind("file://", 0) == 0) {
            LOG_WARN("[callback] SSRF blocked: file:// protocol not allowed (client={}, cmd_id={})", client_id, cmd_id);
            return true;
        }

        // Only allow http:// and https://
        bool isHttp = (lower.rfind("http://", 0) == 0) || (lower.rfind("https://", 0) == 0);
        if (!isHttp) {
            LOG_WARN("[callback] SSRF blocked: non-HTTP protocol (client={}, cmd_id={})", client_id, cmd_id);
            return true;
        }

        size_t host_start = lower.find("://");
        if (host_start == std::string::npos) return true;
        host_start += 3;
        size_t host_end = lower.find_first_of("/?#", host_start);
        std::string host = lower.substr(host_start,
            host_end == std::string::npos ? std::string::npos : host_end - host_start);

        if (host == "localhost" || host == "localhost.")
            return true;
        if (host.rfind("localhost:", 0) == 0)
            return true;

        std::string ip = host;
        size_t colon = ip.find(':');
        if (colon != std::string::npos) ip = ip.substr(0, colon);

        if (ip.rfind("127.", 0) == 0) return true;
        if (ip.rfind("10.", 0) == 0) return true;
        if (ip.rfind("172.", 0) == 0) {
            size_t p1 = ip.find('.');
            size_t p2 = (p1 != std::string::npos) ? ip.find('.', p1 + 1) : std::string::npos;
            if (p1 != std::string::npos && p2 != std::string::npos) {
                int octet2 = atoi(ip.c_str() + p1 + 1);
                if (octet2 >= 16 && octet2 <= 31) return true;
            }
        }
        if (ip.rfind("192.168.", 0) == 0) return true;
        return false;
    }

    std::string safeUrlForLog(const std::string& url) {
        size_t scheme_end = url.find("://");
        if (scheme_end == std::string::npos) return "[invalid-url]";
        size_t host_start = scheme_end + 3;
        size_t path_end = url.find_first_of("/?#", host_start);
        if (path_end == std::string::npos) return url;
        return url.substr(0, path_end);
    }
} // anonymous namespace

void asyncCallback(const std::string& url, const std::string& json_body,
                   const std::string& client_id, const std::string& cmd_id) {

    if (isBlockedUrl(url, client_id, cmd_id)) {
        return;
    }

    // Wait for a semaphore slot (max 10 concurrent)
    {
        std::unique_lock<std::mutex> lock(g_sem_mutex);
        g_sem_cv.wait(lock, [&] { return g_active_callbacks < MAX_CONCURRENT_CALLBACKS; });
        ++g_active_callbacks;
    }

    std::thread([url, json_body, client_id, cmd_id]() {
        const int TIMEOUT_MS = 5000;
        const std::string url_log = safeUrlForLog(url);

        // Strip error_details from body (P2: don't leak system error codes)
        std::string clean_body = json_body;
        try {
            json j = json::parse(json_body);
            j.erase("error_details");
            clean_body = j.dump();
        } catch (...) { /* best-effort */ }

        // ── State machine: 0=init, 1=session, 2=connect, 3=request, 4=response, 5=done ──
        int state = 0;
        HINTERNET hSession = nullptr;
        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;

        // Crack URL (done outside state machine; if it fails we go straight to release)
        {
            std::wstring wurl(url.begin(), url.end());
            URL_COMPONENTS uc = {};
            uc.dwStructSize = sizeof(uc);
            wchar_t hostBuf[1024] = {0};
            wchar_t pathBuf[65521] = {0};
            uc.lpszHostName = hostBuf;
            uc.lpszUrlPath  = pathBuf;
            uc.dwHostNameLength = 1023;
            uc.dwUrlPathLength  = 65520;

            if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
                LOG_WARN("[{}] [callback] WinHttpCrackUrl failed for {} (cmd_id={}, url={})",
                    client_id, url_log, cmd_id, url);
                goto release_slot;
            }

            std::wstring wHost(hostBuf, uc.dwHostNameLength);
            std::wstring wPath(pathBuf, uc.dwUrlPathLength);
            bool isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
            DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
            DWORD dwSecureFlags = 0;

            hSession = WinHttpOpen(
                L"HermesBridgeCallback/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
            if (!hSession) {
                DWORD err = GetLastError();
                LOG_WARN("[{}] [callback] WinHttpOpen failed: err={} (cmd_id={}, url={})",
                    client_id, err, cmd_id, url);
                goto release_slot;
            }
            state = 1;
            WinHttpSetTimeouts(hSession, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);

            hConnect = WinHttpConnect(hSession, wHost.c_str(), uc.nPort, 0);
            if (!hConnect) {
                DWORD err = GetLastError();
                LOG_WARN("[{}] [callback] WinHttpConnect failed: err={} (cmd_id={}, url={})",
                    client_id, err, cmd_id, url);
                goto release_session;
            }
            state = 2;

            hRequest = WinHttpOpenRequest(hConnect, L"POST", wPath.c_str(),
                nullptr, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
            if (!hRequest) {
                DWORD err = GetLastError();
                LOG_WARN("[{}] [callback] WinHttpOpenRequest failed: err={} (cmd_id={}, url={})",
                    client_id, err, cmd_id, url);
                goto release_connect;
            }
            state = 3;

            if (isHttps) {
                dwSecureFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
                    | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
                    | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
                    | SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS,
                    &dwSecureFlags, sizeof(dwSecureFlags));
            }

            BOOL ok = WinHttpSendRequest(hRequest,
                L"Content-Type: application/json",
                (DWORD)-1,
                (LPVOID)clean_body.data(),
                (DWORD)clean_body.size(),
                (DWORD)clean_body.size(), 0);
            if (!ok) {
                DWORD err = GetLastError();
                LOG_WARN("[{}] [callback] WinHttpSendRequest failed: err={} (cmd_id={}, url={})",
                    client_id, err, cmd_id, url);
                goto release_request;
            }

            ok = WinHttpReceiveResponse(hRequest, nullptr);
            if (!ok) {
                DWORD err = GetLastError();
                LOG_WARN("[{}] [callback] WinHttpReceiveResponse failed: err={} (cmd_id={}, url={})",
                    client_id, err, cmd_id, url);
                goto release_request;
            }
            state = 4;

            DWORD status_code = 0;
            DWORD status_code_len = sizeof(status_code);
            WinHttpQueryHeaders(hRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status_code, &status_code_len,
                WINHTTP_NO_HEADER_INDEX);

            // P1: Callback triggered DEBUG log (after ReceiveResponse, before close)
            LOG_DEBUG("[{}] [callback] Callback triggered for cmd {} → {}",
                client_id, cmd_id, url_log);

            WinHttpCloseHandle(hRequest);  hRequest = nullptr;
            WinHttpCloseHandle(hConnect);  hConnect = nullptr;
            WinHttpCloseHandle(hSession);  hSession = nullptr;
            state = 5;

            if (status_code >= 200 && status_code < 300) {
                LOG_INFO("[{}] [callback] POST {} succeeded: HTTP {} (cmd_id={})",
                    client_id, url_log, status_code, cmd_id);
            } else {
                LOG_WARN("[{}] [callback] POST {} failed: HTTP {} (cmd_id={}, url={})",
                    client_id, url_log, status_code, cmd_id, url);
            }
        }

release_slot:
        {
            std::lock_guard<std::mutex> lock(g_sem_mutex);
            --g_active_callbacks;
        }
        g_sem_cv.notify_one();
        return;

        // ── Cleanup labels (must only close handles created after the label point) ──
release_request:
        if (hRequest) { WinHttpCloseHandle(hRequest); hRequest = nullptr; }
        // fall through
release_connect:
        if (hConnect) { WinHttpCloseHandle(hConnect); hConnect = nullptr; }
        // fall through
release_session:
        if (hSession) { WinHttpCloseHandle(hSession); hSession = nullptr; }
        goto release_slot;
    }).detach();
}
