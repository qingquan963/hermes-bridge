#pragma once
#ifndef HERMES_BRIDGE_EXECHANDLER_H
#define HERMES_BRIDGE_EXECHANDLER_H

#include "IHandler.h"

class ExecHandler : public IHandler {
public:
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "exec"; }
};

#endif // HERMES_BRIDGE_EXECHANDLER_H
