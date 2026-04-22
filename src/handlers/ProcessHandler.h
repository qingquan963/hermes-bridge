#pragma once
#ifndef HERMES_BRIDGE_PROCESHANDLER_H
#define HERMES_BRIDGE_PROCESHANDLER_H

#include "IHandler.h"

class ProcessHandler : public IHandler {
public:
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "process_start/process_stop"; }

    static HandlerResult handleStart(const HandlerContext& ctx);
    static HandlerResult handleStop(const HandlerContext& ctx);
};

#endif // HERMES_BRIDGE_PROCESHANDLER_H
