#pragma once
#ifndef HERMES_BRIDGE_IHANDLER_H
#define HERMES_BRIDGE_IHANDLER_H

#include <string>
#include <nlohmann/json.hpp>

struct HandlerContext {
    const nlohmann::json& cmd;
    const std::string& client_id;
    int default_timeout;
};

struct HandlerResult {
    nlohmann::json result;
    std::string error_code;
    std::string error_message;
    std::string error_details;
    bool ok;
    int duration_ms;

    static HandlerResult okResult(nlohmann::json&& res, int duration_ms) {
        HandlerResult r;
        r.ok = true;
        r.result = std::move(res);
        r.duration_ms = duration_ms;
        return r;
    }
    static HandlerResult errorResult(const std::string& code, const std::string& msg,
                                      const std::string& details, int duration_ms) {
        HandlerResult r;
        r.ok = false;
        r.error_code = code;
        r.error_message = msg;
        r.error_details = details;
        r.duration_ms = duration_ms;
        return r;
    }
};

class IHandler {
public:
    virtual ~IHandler() = default;
    virtual HandlerResult handle(const HandlerContext& ctx) = 0;
    virtual std::string actionName() const = 0;
};

#endif // HERMES_BRIDGE_IHANDLER_H
