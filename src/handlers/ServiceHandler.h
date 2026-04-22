#pragma once
#ifndef HERMES_BRIDGE_SERVICEHANDLER_H
#define HERMES_BRIDGE_SERVICEHANDLER_H

#include "IHandler.h"

class ServiceHandler : public IHandler {
public:
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "ps_service_query"; }

    static HandlerResult handleQuery(const HandlerContext& ctx);
};

#endif // HERMES_BRIDGE_SERVICEHANDLER_H
