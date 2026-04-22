#pragma once
#ifndef HERMES_BRIDGE_HTTPHANDLER_H
#define HERMES_BRIDGE_HTTPHANDLER_H

#include "IHandler.h"
#include <string>
#include <windows.h>
#include <winhttp.h>

class HttpHandler : public IHandler {
public:
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "http_get/http_post"; }

    static HandlerResult handleGet(const HandlerContext& ctx);
    static HandlerResult handlePost(const HandlerContext& ctx);

private:
    static bool parseUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& isHttps);
    static std::string wideToUtf8(const std::wstring& wstr);
};

#endif // HERMES_BRIDGE_HTTPHANDLER_H
