#pragma once
#ifndef HERMES_BRIDGE_OLLAMAHANDLER_H
#define HERMES_BRIDGE_OLLAMAHANDLER_H

#include "IHandler.h"

class OllamaHandler : public IHandler {
public:
    explicit OllamaHandler(const std::string& default_url);
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "ollama"; }

private:
    std::string default_url_;
};

#endif // HERMES_BRIDGE_OLLAMAHANDLER_H
