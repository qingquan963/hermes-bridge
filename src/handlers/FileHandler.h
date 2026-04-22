#pragma once
#ifndef HERMES_BRIDGE_FILEHANDLER_H
#define HERMES_BRIDGE_FILEHANDLER_H

#include "IHandler.h"
#include <string>

class FileHandler : public IHandler {
public:
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override;

    static HandlerResult handleRead(const HandlerContext& ctx);
    static HandlerResult handleWrite(const HandlerContext& ctx);
    static HandlerResult handlePatch(const HandlerContext& ctx);
    static std::string actionNameFromCmd(const std::string& action);

private:
    static std::string makeLongPath(const std::string& path);
};

#endif // HERMES_BRIDGE_FILEHANDLER_H
