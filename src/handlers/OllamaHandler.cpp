#include "OllamaHandler.h"
#include "Logger.h"
#include <chrono>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

static std::string wideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) return "";
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

static bool parseUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& isHttps) {
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

OllamaHandler::OllamaHandler(const std::string& default_url) : default_url_(default_url) {}

HandlerResult OllamaHandler::handle(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string model = params.value("model", "");
    std::string prompt = params.value("prompt", "");
    bool stream = params.value("stream", false);

    if (model.empty() || prompt.empty()) {
        return HandlerResult::errorResult("INVALID_REQUEST", "model and prompt are required", "", 0);
    }

    nlohmann::json req_body;
    req_body["model"] = model;
    req_body["prompt"] = prompt;
    req_body["stream"] = stream;
    if (params.contains("options")) {
        req_body["options"] = params["options"];
    }
    std::string body = req_body.dump();

    std::wstring host, wpath;
    INTERNET_PORT port;
    bool isHttps;
    if (!parseUrl(default_url_, host, wpath, port, isHttps)) {
        return HandlerResult::errorResult("OLLAMA_ERROR", "Failed to parse Ollama URL: " + default_url_, "", 0);
    }

    DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;

    // P1-1: Read timeout from params, fall back to context default
    int timeout_sec = params.value("timeout", ctx.default_timeout);
    if (timeout_sec <= 0) timeout_sec = 300; // minimum 300s (5 min) to support slow models like qwen3.5:4b
    HINTERNET hSession = WinHttpOpen(L"HermesBridge/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return HandlerResult::errorResult("OLLAMA_ERROR", "WinHttpOpen failed", "", 0);

    WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 2000); // receive timeout doubled for slow responses

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return HandlerResult::errorResult("OLLAMA_ERROR", "WinHttpConnect failed", "", 0); }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return HandlerResult::errorResult("OLLAMA_ERROR", "WinHttpOpenRequest failed", "", 0); }

    // P1-2: No custom security flags — use WinHTTP default certificate validation
    // (removed dwSecureFlags that disabled HTTPS certificate checks)

    std::wstring wContentType = L"Content-Type: application/json";
    BOOL bResults = WinHttpSendRequest(hRequest, wContentType.c_str(), (DWORD)-1,
        body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("OLLAMA_ERROR", "WinHttpSendRequest failed, error=" + std::to_string(err), "", 0);
    }

    bResults = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResults) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
        return HandlerResult::errorResult("OLLAMA_ERROR", "WinHttpReceiveResponse failed, error=" + std::to_string(err), "", 0);
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

    if (status_code != 200) {
        return HandlerResult::errorResult("OLLAMA_ERROR",
            "Ollama returned HTTP " + std::to_string(status_code),
            response_body, duration_ms);
    }

    nlohmann::json response;
    try {
        response = nlohmann::json::parse(response_body);
    } catch (...) {
        return HandlerResult::errorResult("OLLAMA_ERROR", "Failed to parse Ollama response", response_body, duration_ms);
    }

    nlohmann::json res_json;
    res_json["model"] = model;
    res_json["response"] = response.value("response", "");
    res_json["done"] = response.value("done", true);
    res_json["total_duration_ms"] = response.value("total_duration", 0);

    return HandlerResult::okResult(std::move(res_json), duration_ms);
}
