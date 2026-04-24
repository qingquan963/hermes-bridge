#pragma once
#ifndef HERMES_BRIDGE_TCPPROXYHANDLER_H
#define HERMES_BRIDGE_TCPPROXYHANDLER_H

#include "IHandler.h"
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <winsock2.h>

struct TcpConnection {
    std::string connection_id;
    SOCKET sock;
    std::thread read_thread;
    std::atomic<bool> active;
    std::string host;
    int port;
    std::string client_id;
};

class TcpProxyHandler : public IHandler {
public:
    TcpProxyHandler();
    ~TcpProxyHandler();
    HandlerResult handle(const HandlerContext& ctx) override;
    std::string actionName() const override { return "tcp_connect"; }

    void setWorkDir(const std::string& dir) { work_dir_ = dir; }

    // Called by bridge to send data on an existing connection
    HandlerResult sendData(const std::string& connection_id, const std::string& data);

private:
    std::string createConnection(const std::string& host, int port, const std::string& client_id);
    void closeConnection(const std::string& connection_id);
    void readLoop(const std::string& connection_id, SOCKET sock, const std::string& host, int port, const std::string& client_id);
    void writeCallback(const std::string& client_id, const std::string& connection_id,
                       const std::string& host, int port, const std::string& status,
                       const std::string& data = "", bool is_binary = false,
                       const std::vector<uint8_t>& binary_data = {});

    std::map<std::string, std::shared_ptr<TcpConnection>> connections_;
    std::mutex mutex_;
    std::string work_dir_;
};

#endif // HERMES_BRIDGE_TCPPROXYHANDLER_H
