#pragma once

// Force ws2_32 to load before windows.h / winsock.h
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK2_H
#define _WINSOCK2_H
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <string>
#include <memory>
#include <atomic>
#include <thread>

class CallbackWriter;

class HttpServer {
public:
    HttpServer(const std::string& bind_host, int port, const std::string& callbacks_dir);
    ~HttpServer();

    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

private:
    void runLoop();
    void handleClient(SOCKET client_sock);

    bool parseRequestLine(const char* buf, int len,
        std::string& method, std::string& path, std::string& version);
    bool validateJson(const char* body, int body_len);

    void sendResponse(SOCKET sock, int status_code,
        const std::string& status_text,
        const std::string& body = "");

    SOCKET createListeningSocket();
    bool bindAndListen(SOCKET sock, const std::string& host, int port);

    std::string bind_host_;
    int port_;
    std::string callbacks_dir_;

    SOCKET listen_socket_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    std::unique_ptr<CallbackWriter> writer_;
};