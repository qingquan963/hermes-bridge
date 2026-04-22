#include "FileMonitor.h"
#include "Logger.h"
#include <windows.h>
#include <chrono>
#include <algorithm>

// Include nlohmann/json here since we use it directly
#include <nlohmann/json.hpp>

FileMonitor::FileMonitor(const Config& config, CommandQueue& queue)
    : config_(config), queue_(queue) {}

FileMonitor::~FileMonitor() {
    stop();
}

void FileMonitor::start() {
    if (running_.load()) return;
    stop_.store(false);
    running_.store(true);
    thread_ = std::thread(&FileMonitor::pollLoop, this);
    LOG_INFO("FileMonitor started (poll_interval=" + std::to_string(config_.poll_interval_ms) + "ms)");
}

void FileMonitor::stop() {
    if (!running_.load()) return;
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
    running_.store(false);
    LOG_INFO("FileMonitor stopped");
}

bool FileMonitor::isJsonComplete(const std::string& content) const {
    // Rule 1: non-empty (whitespace-only fails)
    auto pos = content.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return false;

    // Rule 2: must start with { or [
    char first_char = content[pos];
    if (first_char != '{' && first_char != '[') return false;

    // Rule 3: parseable
    try {
        nlohmann::json::parse(content);
        return true;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
}

std::string FileMonitor::readCmdFile(const std::string& path) const {
    // Try long path prefix
    std::wstring wpath = L"\\\\?\\" + std::wstring(path.begin(), path.end());

    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        // Try without long path prefix
        h = CreateFileW(std::wstring(path.begin(), path.end()).c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return "";
    }

    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);

    // Check max size
    if (size.QuadPart > config_.max_request_size_kb * 1024LL) {
        CloseHandle(h);
        return "";
    }

    std::string content(static_cast<size_t>(size.QuadPart), '\0');
    if (size.QuadPart > 0) {
        DWORD bytesRead = 0;
        ReadFile(h, &content[0], static_cast<DWORD>(size.QuadPart), &bytesRead, NULL);
        content.resize(bytesRead);
    }
    CloseHandle(h);
    return content;
}

bool FileMonitor::truncateFile(const std::string& path) const {
    // Try long path
    std::wstring wpath = L"\\\\?\\" + std::wstring(path.begin(), path.end());
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        // Try without long path
        h = CreateFileW(std::wstring(path.begin(), path.end()).c_str(), GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, TRUNCATE_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) return false;
    }
    CloseHandle(h);
    return true;
}

bool FileMonitor::scanAndEnqueue() {
    WIN32_FIND_DATAW ffd;
    std::wstring searchPattern = std::wstring(config_.work_dir.begin(), config_.work_dir.end()) + L"\\cmd_*.txt";
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return true; // No cmd files, not an error

    bool any_error = false;
    do {
        std::wstring fname(ffd.cFileName);
        if (fname.rfind(L"cmd_", 0) != 0) continue;

        // Convert fname to narrow string
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, fname.c_str(), -1, NULL, 0, NULL, NULL);
        std::string narrow_fname(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, fname.c_str(), -1, &narrow_fname[0], size_needed, NULL, NULL);

        std::string full_path = config_.work_dir + "\\" + narrow_fname;

        // client_id: strip "cmd_" prefix and ".txt" suffix
        // narrow_fname = "cmd_CLIENT.txt\r\n" (may have trailing \r\n and embedded null)
        std::string client_id = narrow_fname.substr(4); // after "cmd_"
        // Step 1: strip trailing \r\n and null chars first
        while (!client_id.empty() && (client_id.back() == '\r' || client_id.back() == '\n' || client_id.back() == '\0' || client_id.back() == ' ')) {
            client_id.pop_back();
        }
        // Step 2: strip ".txt" suffix (4 chars), only if result is non-empty
        if (client_id.size() >= 4 && client_id.substr(client_id.size() - 4) == ".txt") {
            client_id.resize(client_id.size() - 4);
        }

        std::string content = readCmdFile(full_path);
        if (content.empty()) continue;

        if (!isJsonComplete(content)) {
            LOG_WARN("[{}] Incomplete JSON, skipping (file={})", client_id, full_path);
            continue;
        }

        // Parse JSON (may be array or object)
        try {
            nlohmann::json cmds = nlohmann::json::parse(content);
            if (!cmds.is_array()) cmds = nlohmann::json::array({cmds});

            for (const auto& cmd_json : cmds) {
                Command cmd;
                cmd.cmd_id = cmd_json.value("cmd_id", "unknown");
                // Explicitly convert to std::string to avoid any nlohmann json reference issues
                std::string action_str = cmd_json.value("action", "");
                cmd.action = action_str;
                cmd.params = cmd_json.value("params", nlohmann::json::object());
                cmd.timeout = cmd_json.value("timeout", config_.default_timeout_sec);
                cmd.timestamp = cmd_json.value("timestamp", "");
                cmd.client_id = client_id;
                // Capture action before move (avoid use-after-move)
                std::string action_for_log = cmd.action;
                std::string cmd_id_for_log = cmd.cmd_id;
                queue_.enqueue(std::move(cmd));
                LOG_INFO("[{}] Enqueued cmd {} (action={})", client_id, cmd_id_for_log, action_for_log);
            }

            // Truncate cmd file after successful processing
            truncateFile(full_path);

            // Track client
            {
                std::lock_guard<std::mutex> lock(clients_mtx_);
                clients_.insert(client_id);
            }
        } catch (const std::exception& e) {
            LOG_WARN("[{}] Exception parsing cmd file: {}", client_id, e.what());
            any_error = true;
        }

    } while (FindNextFileW(hFind, &ffd));

    FindClose(hFind);
    return !any_error;
}

void FileMonitor::pollLoop() {
    while (!stop_.load()) {
        auto poll_start = std::chrono::steady_clock::now();

        scanAndEnqueue();

        auto poll_end = std::chrono::steady_clock::now();
        int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            poll_end - poll_start).count());

        // Sleep for the remainder of the poll interval
        int sleep_ms = config_.poll_interval_ms - elapsed;
        if (sleep_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        } else {
            std::this_thread::yield();
        }
    }
}

std::vector<std::string> FileMonitor::discoveredClients() const {
    std::lock_guard<std::mutex> lock(clients_mtx_);
    return std::vector<std::string>(clients_.begin(), clients_.end());
}
