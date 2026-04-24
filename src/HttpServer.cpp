#include "HttpServer.h"
#include "CallbackWriter.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <sstream>
#include <cctype>

#pragma comment(lib, "ws2_32.lib")

namespace HttpServerConfig {
    constexpr int  MAX_BODY_SIZE   = 65536;   // 64KB
    constexpr int  RECV_BUF_SIZE   = MAX_BODY_SIZE + 4096;
    constexpr int  ACCEPT_BACKLOG  = 16;
}

// Case-insensitive strstr
static const char* stristr(const char* haystack, const char* needle) {
    if (!needle[0]) return haystack;
    for (const char* p = haystack; *p; ++p) {
        if (::_strnicmp(p, needle, strlen(needle)) == 0) return p;
    }
    return nullptr;
}

HttpServer::HttpServer(const std::string& bind_host, int port, const std::string& callbacks_dir)
    : bind_host_(bind_host), port_(port), callbacks_dir_(callbacks_dir) {}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start() {
    if (running_.load()) return true;

    writer_ = std::make_unique<CallbackWriter>(callbacks_dir_);
    if (!writer_->init()) {
        LOG_ERROR("[httpserver] CallbackWriter init failed");
        return false;
    }

    listen_socket_ = createListeningSocket();
    if (listen_socket_ == INVALID_SOCKET) return false;

    if (!bindAndListen(listen_socket_, bind_host_, port_)) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&HttpServer::runLoop, this);
    LOG_INFO("[httpserver] HTTP Server started on " + bind_host_ + ":" + std::to_string(port_));
    return true;
}

void HttpServer::stop() {
    if (!running_.load()) return;
    running_ = false;

    if (listen_socket_ != INVALID_SOCKET) {
        shutdown(listen_socket_, SD_BOTH);
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    LOG_INFO("[httpserver] HTTP Server stopped");
}

SOCKET HttpServer::createListeningSocket() {
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("[httpserver] WSASocketW failed: " + std::to_string(WSAGetLastError()));
        return INVALID_SOCKET;
    }

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    return s;
}

bool HttpServer::bindAndListen(SOCKET sock, const std::string& host, int port) {
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));

    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("[httpserver] inet_pton failed for: " + host);
        return false;
    }

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("[httpserver] bind failed: " + std::to_string(WSAGetLastError()));
        return false;
    }

    if (listen(sock, HttpServerConfig::ACCEPT_BACKLOG) == SOCKET_ERROR) {
        LOG_ERROR("[httpserver] listen failed: " + std::to_string(WSAGetLastError()));
        return false;
    }

    return true;
}

void HttpServer::runLoop() {
    while (running_.load()) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_socket_, &read_fds);

        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(0, &read_fds, NULL, NULL, &tv);
        if (ret == SOCKET_ERROR) {
            if (running_.load()) {
                LOG_ERROR("[httpserver] select failed: " + std::to_string(WSAGetLastError()));
            }
            break;
        }
        if (ret == 0) continue;

        if (FD_ISSET(listen_socket_, &read_fds)) {
            sockaddr_in client_addr;
            int addr_len = sizeof(client_addr);
            SOCKET client_sock = accept(listen_socket_, (sockaddr*)&client_addr, &addr_len);
            if (client_sock == INVALID_SOCKET) {
                if (running_.load()) {
                    LOG_ERROR("[httpserver] accept failed: " + std::to_string(WSAGetLastError()));
                }
                continue;
            }

            std::thread(&HttpServer::handleClient, this, client_sock).detach();
        }
    }
}

static int skipWhitespace(const char* p) {
    while (*p == ' ' || *p == '\t') ++p;
    return static_cast<int>(p - (const char*)0);
}

void HttpServer::handleClient(SOCKET client_sock) {
    char recv_buf[HttpServerConfig::RECV_BUF_SIZE];
    int total_recv = 0;

    while (total_recv < HttpServerConfig::RECV_BUF_SIZE) {
        int n = recv(client_sock, recv_buf + total_recv,
                     HttpServerConfig::RECV_BUF_SIZE - total_recv, 0);
        if (n <= 0) {
            closesocket(client_sock);
            return;
        }
        total_recv += n;

        if (total_recv >= 4) {
            for (int i = 0; i <= total_recv - 4; ++i) {
                if (recv_buf[i] == '\r' && recv_buf[i+1] == '\n' &&
                    recv_buf[i+2] == '\r' && recv_buf[i+3] == '\n') {
                    recv_buf[i] = '\0';
                    int body_start = i + 4;  // \r\n\r\n ends at i,i+1,i+2,i+3, body starts at i+4
                    int body_received = total_recv - (i + 4);

                    int content_length = 0;
                    const char* cl = stristr(recv_buf, "Content-Length:");
                    if (cl) {
                        cl += 14;  // skip "Content-Length:"
                        while (*cl == ' ' || *cl == '\t') ++cl;
                        content_length = atoi(cl);
                    }

                    std::string method, path, version;
                    if (!parseRequestLine(recv_buf, i, method, path, version)) {
                        sendResponse(client_sock, 400, "Bad Request", R"({"status":"error","message":"Invalid request line"})");
                        closesocket(client_sock);
                        return;
                    }

                    if (method != "POST") {
                        sendResponse(client_sock, 405, "Method Not Allowed", R"({"status":"error","message":"Method not allowed"})");
                        closesocket(client_sock);
                        return;
                    }

                    if (path != "/callback") {
                        sendResponse(client_sock, 404, "Not Found", R"({"status":"error","message":"Not found"})");
                        closesocket(client_sock);
                        return;
                    }

                    // Case-insensitive Content-Type check
                    const char* ct = stristr(recv_buf, "Content-Type:");
                    if (!ct) {
                        sendResponse(client_sock, 400, "Bad Request", R"({"status":"error","message":"Content-Type must be application/json"})");
                        closesocket(client_sock);
                        return;
                    }
                    // Find colon of the header
                    const char* colon = ct;
                    while (*colon != ':' && colon < recv_buf + i) ++colon;
                    const char* val = colon + 1;
                    while (*val == ' ' || *val == '\t') ++val;
                    // Accept "application/json" with optional charset/params (e.g. "; charset=utf-8")
                    // val[16] can be: '\0' (end), ';' (params), ' ' (trailing space), or '\r' (end of header line)
                    bool is_json = (::_strnicmp(val, "application/json", 16) == 0) &&
                                   (val[16] == '\0' || val[16] == ';' || val[16] == ' ' || val[16] == '\r');
                    if (!is_json) {
                        sendResponse(client_sock, 400, "Bad Request", R"({"status":"error","message":"Content-Type must be application/json"})");
                        closesocket(client_sock);
                        return;
                    }

                    // Early 413 check: reject based on Content-Length header BEFORE receiving body
                    // This prevents buffer overflow for requests with huge Content-Length
                    if (content_length > HttpServerConfig::MAX_BODY_SIZE) {
                        sendResponse(client_sock, 413, "Payload Too Large", R"({"status":"error","message":"Request body too large"})");
                        closesocket(client_sock);
                        return;
                    }

                    std::string body;
                    body.reserve(content_length);
                    body.append(recv_buf + body_start, body_received);

                    while (static_cast<int>(body.size()) < content_length) {
                        int need = content_length - static_cast<int>(body.size());
                        int n2 = recv(client_sock, recv_buf, (std::min)(need, HttpServerConfig::RECV_BUF_SIZE), 0);
                        if (n2 <= 0) break;
                        body.append(recv_buf, n2);
                    }

                    if (static_cast<int>(body.size()) < content_length) {
                        sendResponse(client_sock, 400, "Bad Request", R"({"status":"error","message":"Incomplete body"})");
                        closesocket(client_sock);
                        return;
                    }

                    // 413 check AFTER body reception (before JSON validation)
                    if (static_cast<int>(body.size()) > HttpServerConfig::MAX_BODY_SIZE) {
                        sendResponse(client_sock, 413, "Payload Too Large", R"({"status":"error","message":"Request body too large"})");
                        closesocket(client_sock);
                        return;
                    }

                    if (!validateJson(body.data(), static_cast<int>(body.size()))) {
                        sendResponse(client_sock, 400, "Bad Request", R"({"status":"error","message":"Invalid JSON"})");
                        closesocket(client_sock);
                        return;
                    }

                    std::string client = "unknown";
                    try {
                        auto j = json::parse(body);
                        if (j.contains("client") && j["client"].is_string())
                            client = j["client"];
                    } catch (...) {}

                    if (!writer_->writeCallback(body, client)) {
                        sendResponse(client_sock, 500, "Internal Server Error", R"({"status":"error","message":"Failed to write callback"})");
                        closesocket(client_sock);
                        return;
                    }

                    sendResponse(client_sock, 200, "OK", R"({"status":"ok"})");
                    closesocket(client_sock);
                    return;
                }
            }
        }
    }

    sendResponse(client_sock, 413, "Payload Too Large", R"({"status":"error","message":"Request body too large"})");
    closesocket(client_sock);
}

bool HttpServer::parseRequestLine(const char* buf, int /*len*/,
    std::string& method, std::string& path, std::string& version) {
    const char* eol = buf;
    while (eol < buf + 8192 && !(eol[0] == '\r' && eol[1] == '\n')) ++eol;
    if (eol >= buf + 8192) return false;

    std::string line(buf, eol - buf);
    std::istringstream iss(line);
    if (!(iss >> method >> path >> version)) return false;

    return !method.empty() && !path.empty();
}

bool HttpServer::validateJson(const char* body, int body_len) {
    if (body_len < 2) return false;
    if (body[0] != '{' || body[body_len - 1] != '}') return false;
    try {
        json::parse(body, body + body_len);
        return true;
    } catch (...) {
        return false;
    }
}

void HttpServer::sendResponse(SOCKET sock, int status_code,
    const std::string& status_text,
    const std::string& body) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    std::string resp = oss.str();
    send(sock, resp.c_str(), static_cast<int>(resp.size()), 0);
}
