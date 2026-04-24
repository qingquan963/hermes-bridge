#include "TcpProxyHandler.h"
#include "Logger.h"
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <vector>
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include <algorithm>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

static std::string generateConnectionId() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned __int64 ts = (static_cast<unsigned __int64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    std::ostringstream oss;
    oss << std::hex << ts;
    return oss.str();
}

static std::string getSocketError() {
    int err = WSAGetLastError();
    char* msg = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg, 0, nullptr);
    if (msg) {
        std::string str(msg);
        LocalFree(msg);
        // Remove trailing newline
        while (!str.empty() && (str.back() == '\n' || str.back() == '\r')) {
            str.pop_back();
        }
        return str;
    }
    return "error=" + std::to_string(err);
}

TcpProxyHandler::TcpProxyHandler() {}

TcpProxyHandler::~TcpProxyHandler() {
    // Close all connections
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& kv : connections_) {
        auto& conn = kv.second;
        if (conn->sock != INVALID_SOCKET) {
            conn->active = false;
            closesocket(conn->sock);
            conn->sock = INVALID_SOCKET;
        }
        if (conn->read_thread.joinable()) {
            conn->read_thread.join();
        }
    }
    connections_.clear();
}

void TcpProxyHandler::writeCallback(const std::string& client_id,
                                    const std::string& connection_id,
                                    const std::string& host, int port,
                                    const std::string& status,
                                    const std::string& data,
                                    bool is_binary,
                                    const std::vector<uint8_t>& binary_data) {
    json obj;
    obj["type"] = "tcp_" + status;
    obj["connection_id"] = connection_id;
    obj["host"] = host;
    obj["port"] = port;
    obj["client_id"] = client_id;

    if (status == "connected" || status == "error" || status == "closed") {
        obj["status"] = status;
    }

    if (!data.empty()) {
        obj["data"] = data;
    }

    if (is_binary && !binary_data.empty()) {
        // Base64 encode binary data
        static const char* base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string base64;
        int i = 0;
        size_t len = binary_data.size();

        while (i < static_cast<int>(len)) {
            unsigned int octet_a = (i < static_cast<int>(len)) ? binary_data[i] : 0;
            unsigned int octet_b = (i + 1 < static_cast<int>(len)) ? binary_data[i + 1] : 0;
            unsigned int octet_c = (i + 2 < static_cast<int>(len)) ? binary_data[i + 2] : 0;

            unsigned int triple = (octet_a << 16) + (octet_b << 8) + octet_c;

            base64 += base64_chars[(triple >> 18) & 0x3F];
            base64 += base64_chars[(triple >> 12) & 0x3F];
            base64 += (i + 1 < static_cast<int>(len)) ? base64_chars[(triple >> 6) & 0x3F] : '=';
            base64 += (i + 2 < static_cast<int>(len)) ? base64_chars[triple & 0x3F] : '=';

            i += 3;
        }
        obj["data_b64"] = base64;
        obj["binary"] = true;
    }

    std::string json_str = obj.dump();

    // Write to callbacks directory
    std::string callbacks_dir = work_dir_ + "\\callbacks\\";
    CreateDirectoryA(callbacks_dir.c_str(), nullptr);

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned __int64 ts = (static_cast<unsigned __int64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    ts /= 10000; // milliseconds

    std::string safe_client = client_id;
    for (char& c : safe_client) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }

    std::string filename = std::to_string(ts) + "_" + safe_client + "_" + connection_id + ".json";
    std::string tmp_path = callbacks_dir + filename + ".tmp";
    std::string final_path = callbacks_dir + filename;

    HANDLE h = CreateFileA(tmp_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, json_str.data(), static_cast<DWORD>(json_str.size()), &written, nullptr);
        FlushFileBuffers(h);
        CloseHandle(h);
        MoveFileExA(tmp_path.c_str(), final_path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        LOG_DEBUG("[tcp] Callback written: {} for conn {}", filename, connection_id);
    } else {
        LOG_ERROR("[tcp] Failed to write callback: " + tmp_path);
    }
}

std::string TcpProxyHandler::createConnection(const std::string& host, int port, const std::string& client_id) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        LOG_ERROR("[tcp] socket() failed: " + getSocketError());
        return "";
    }

    // Set socket to non-blocking for connect with timeout
    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));

    // Try to convert address using inet_pton
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        // Try getaddrinfo for hostname resolution
        addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        int ret = getaddrinfo(host.c_str(), nullptr, &hints, &result);
        if (ret != 0 || result == nullptr) {
            LOG_ERROR("[tcp] Invalid address: " + host);
            closesocket(sock);
            return "";
        }
        memcpy(&addr.sin_addr, &((sockaddr_in*)result->ai_addr)->sin_addr, sizeof(addr.sin_addr));
        freeaddrinfo(result);
    }

    // Connect with timeout
    int result = connect(sock, (sockaddr*)&addr, sizeof(addr));
    if (result == SOCKET_ERROR) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(sock, &write_fds);
        timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;

        result = select(0, nullptr, &write_fds, nullptr, &tv);
        if (result <= 0) {
            LOG_ERROR("[tcp] connect() timeout or error for " + host + ":" + std::to_string(port));
            closesocket(sock);
            return "";
        }

        // Check if actually connected
        int err = 0;
        int err_len = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &err_len);
        if (err != 0) {
            LOG_ERROR("[tcp] connect() failed: " + std::to_string(err));
            closesocket(sock);
            return "";
        }
    }

    // Set back to blocking for recv
    mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);

    // Generate connection ID
    std::string connection_id = generateConnectionId();

    // Create connection object
    auto conn = std::make_shared<TcpConnection>();
    conn->connection_id = connection_id;
    conn->sock = sock;
    conn->active = true;
    conn->host = host;
    conn->port = port;
    conn->client_id = client_id;

    // Store in map
    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_[connection_id] = conn;
    }

    // Start read thread
    conn->read_thread = std::thread([this, connection_id, sock, host, port, client_id]() {
        readLoop(connection_id, sock, host, port, client_id);
    });

    LOG_INFO("[tcp] Connection established: {} -> {}:{}", connection_id, host, port);
    return connection_id;
}

void TcpProxyHandler::readLoop(const std::string& connection_id, SOCKET sock,
                               const std::string& host, int port, const std::string& client_id) {
    char buffer[8192];

    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(sock, &read_fds);
        timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int result = select(0, &read_fds, nullptr, nullptr, &tv);
        if (!result) continue; // Timeout, check again
        if (result == SOCKET_ERROR) {
            LOG_ERROR("[tcp] select() error for conn " + connection_id);
            break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            int bytes = recv(sock, buffer, sizeof(buffer), 0);
            if (bytes > 0) {
                // Copy to vector for binary data
                std::vector<uint8_t> data_vec(buffer, buffer + bytes);
                writeCallback(client_id, connection_id, host, port, "data", "",
                    true, data_vec);
            } else if (bytes == 0) {
                // Connection closed
                LOG_INFO("[tcp] Connection closed: " + connection_id);
                writeCallback(client_id, connection_id, host, port, "closed");
                break;
            } else {
                // Error
                LOG_ERROR("[tcp] recv() error for conn " + connection_id + ": " + getSocketError());
                writeCallback(client_id, connection_id, host, port, "error", getSocketError());
                break;
            }
        }
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = connections_.find(connection_id);
        if (it != connections_.end()) {
            if (it->second->sock != INVALID_SOCKET) {
                closesocket(it->second->sock);
                it->second->sock = INVALID_SOCKET;
            }
            connections_.erase(it);
        }
    }
}

void TcpProxyHandler::closeConnection(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(connection_id);
    if (it != connections_.end()) {
        auto& conn = it->second;
        conn->active = false;
        if (conn->sock != INVALID_SOCKET) {
            closesocket(conn->sock);
            conn->sock = INVALID_SOCKET;
        }
        connections_.erase(it);
        LOG_INFO("[tcp] Connection closed: " + connection_id);
    }
}

HandlerResult TcpProxyHandler::sendData(const std::string& connection_id, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = connections_.find(connection_id);
    if (it == connections_.end()) {
        return HandlerResult::errorResult("CONNECTION_NOT_FOUND",
            "Connection not found: " + connection_id, "", 0);
    }

    SOCKET sock = it->second->sock;
    if (sock == INVALID_SOCKET) {
        return HandlerResult::errorResult("CONNECTION_CLOSED",
            "Connection closed: " + connection_id, "", 0);
    }

    int sent = send(sock, data.data(), static_cast<int>(data.size()), 0);
    if (sent == SOCKET_ERROR) {
        return HandlerResult::errorResult("SEND_FAILED",
            "send() failed", getSocketError(), 0);
    }

    json res;
    res["sent"] = sent;
    res["connection_id"] = connection_id;
    return HandlerResult::okResult(std::move(res), 0);
}

HandlerResult TcpProxyHandler::handle(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    std::string action = ctx.cmd.value("action", "tcp_connect");

    if (action == "tcp_send") {
        const auto& params = ctx.cmd.value("params", nlohmann::json::object());
        std::string connection_id = params.value("connection_id", "");
        std::string data = params.value("data", "");

        if (connection_id.empty()) {
            return HandlerResult::errorResult("INVALID_REQUEST",
                "connection_id is required for tcp_send", "", 0);
        }
        if (data.empty()) {
            return HandlerResult::errorResult("INVALID_REQUEST",
                "data is required for tcp_send", "", 0);
        }

        HandlerResult res = sendData(connection_id, data);
        res.duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count());
        return res;
    }

    // tcp_connect or other action: create connection
    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string host = params.value("host", "");
    int port = params.value("port", 0);

    if (host.empty()) {
        return HandlerResult::errorResult("INVALID_REQUEST", "host is required", "", 0);
    }
    if (port <= 0 || port > 65535) {
        return HandlerResult::errorResult("INVALID_REQUEST", "port must be between 1 and 65535", "", 0);
    }

    // Create connection
    std::string connection_id = createConnection(host, port, ctx.client_id);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    if (connection_id.empty()) {
        return HandlerResult::errorResult("CONNECT_FAILED",
            "Failed to connect to " + host + ":" + std::to_string(port),
            getSocketError(), duration_ms);
    }

    // Write connected callback
    writeCallback(ctx.client_id, connection_id, host, port, "connected");

    json res;
    res["connection_id"] = connection_id;
    res["host"] = host;
    res["port"] = port;
    res["status"] = "connected";

    return HandlerResult::okResult(std::move(res), duration_ms);
}
