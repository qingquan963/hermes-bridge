#include "HttpHandler.h"
#include "Logger.h"
#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

std::string HttpHandler::wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

bool HttpHandler::parseUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& isHttps) {
    std::wstring wurl = std::wstring(url.begin(), url.end());

    URL_COMPONENTS uc = {};
    uc.dwStructSize = sizeof(uc);
    const DWORD MAX_URL_LEN = 65521;
    wchar_t hostBuffer[MAX_URL_LEN];
    wchar_t pathBuffer[MAX_URL_LEN];

    uc.lpszHostName = hostBuffer;
    uc.lpszUrlPath = pathBuffer;
    uc.dwHostNameLength = MAX_URL_LEN;
    uc.dwUrlPathLength = MAX_URL_LEN;

    if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) return false;

    host = std::wstring(hostBuffer, uc.dwHostNameLength);
    path = std::wstring(pathBuffer, uc.dwUrlPathLength);
    port = uc.nPort;
    isHttps = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

HandlerResult HttpHandler::handle(const HandlerContext& ctx) {
    std::string action = ctx.cmd.value("action", "");
    if (action == "http_get") return handleGet(ctx);
    if (action == "http_post") return handlePost(ctx);
    return HandlerResult::errorResult("INVALID_REQUEST", "Unknown http action: " + action, "", 0);
}

HandlerResult HttpHandler::handleGet(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string url = params.value("url", "");
    int timeout_sec = params.value("timeout", 10);

    if (url.empty()) return HandlerResult::errorResult("INVALID_REQUEST", "url is required", "", 0);

    std::wstring host, wpath;
    INTERNET_PORT port;
    bool isHttps;
    if (!parseUrl(url, host, wpath, port, isHttps)) {
        return HandlerResult::errorResult("HTTP_ERROR", "Failed to parse URL: " + url, url, 0);
    }

    DWORD dwFlags = isHttps ? (WINHTTP_FLAG_SECURE) : 0;
    DWORD dwSecureFlags = 0;

    HINTERNET hSession = WinHttpOpen(L"HermesBridge/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpOpen failed", url, 0);
    }

    WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpConnect failed", url, 0);
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        dwFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpOpenRequest failed", url, 0);
    }

    if (isHttps) {
        dwSecureFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
            | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
            | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
            | SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecureFlags, sizeof(dwSecureFlags));
    }

    BOOL bResults = WinHttpSendRequest(hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);

    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR",
            "WinHttpSendRequest failed, error=" + std::to_string(err), url, 0);
    }

    bResults = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR",
            "WinHttpReceiveResponse failed, error=" + std::to_string(err), url, 0);
    }

    DWORD status_code = 0;
    DWORD status_code_len = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_len, WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    std::string read_buf(8192, '\0');
    DWORD bytes_read = 0;
    while (WinHttpReadData(hRequest, &read_buf[0], (DWORD)read_buf.size(), &bytes_read) && bytes_read > 0) {
        response_body.append(read_buf.data(), bytes_read);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res_json;
    res_json["status_code"] = static_cast<int>(status_code);
    res_json["body"] = response_body;
    res_json["time_ms"] = duration_ms;
    return HandlerResult::okResult(std::move(res_json), duration_ms);
}

HandlerResult HttpHandler::handlePost(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string url = params.value("url", "");
    std::string body = params.value("body", "");
    int timeout_sec = params.value("timeout", 10);

    if (url.empty()) return HandlerResult::errorResult("INVALID_REQUEST", "url is required", "", 0);

    if (params.contains("json") && body.empty()) {
        body = params["json"].dump();
    }

    std::wstring host, wpath;
    INTERNET_PORT port;
    bool isHttps;
    if (!parseUrl(url, host, wpath, port, isHttps)) {
        return HandlerResult::errorResult("HTTP_ERROR", "Failed to parse URL: " + url, url, 0);
    }

    DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    DWORD dwSecureFlags = 0;

    HINTERNET hSession = WinHttpOpen(L"HermesBridge/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpOpen failed", url, 0);
    }

    WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpConnect failed", url, 0);
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        dwFlags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR", "WinHttpOpenRequest failed", url, 0);
    }

    if (isHttps) {
        dwSecureFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA
            | SECURITY_FLAG_IGNORE_CERT_CN_INVALID
            | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID
            | SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &dwSecureFlags, sizeof(dwSecureFlags));
    }

    std::wstring wContentType = L"Content-Type: application/json";
    std::wstring wContentLength = L"Content-Length: " + std::to_wstring(body.size());

    BOOL bResults = WinHttpSendRequest(hRequest,
        wContentType.c_str(), (DWORD)-1,
        body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);

    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR",
            "WinHttpSendRequest failed, error=" + std::to_string(err), url, 0);
    }

    bResults = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("HTTP_ERROR",
            "WinHttpReceiveResponse failed, error=" + std::to_string(err), url, 0);
    }

    DWORD status_code = 0;
    DWORD status_code_len = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_code_len, WINHTTP_NO_HEADER_INDEX);

    std::string response_body;
    std::string read_buf(8192, '\0');
    DWORD bytes_read = 0;
    while (WinHttpReadData(hRequest, &read_buf[0], (DWORD)read_buf.size(), &bytes_read) && bytes_read > 0) {
        response_body.append(read_buf.data(), bytes_read);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res_json;
    res_json["status_code"] = static_cast<int>(status_code);
    res_json["body"] = response_body;
    res_json["time_ms"] = duration_ms;
    return HandlerResult::okResult(std::move(res_json), duration_ms);
}
