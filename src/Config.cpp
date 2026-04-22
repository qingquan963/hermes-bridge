#include "Config.h"
#include <fstream>
#include <filesystem>
#include <sstream>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// nlohmann/json header-only
#include <nlohmann/json.hpp>
using json = nlohmann::json;

bool Config::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    try {
        json cfg = json::parse(content);
        if (cfg.contains("version")) version = cfg["version"].get<std::string>();
        if (cfg.contains("poll_interval_ms")) poll_interval_ms = cfg["poll_interval_ms"].get<int>();
        if (cfg.contains("worker_count")) worker_count = cfg["worker_count"].get<int>();
        if (cfg.contains("default_timeout_sec")) default_timeout_sec = cfg["default_timeout_sec"].get<int>();
        if (cfg.contains("log_dir")) log_dir = cfg["log_dir"].get<std::string>();
        if (cfg.contains("log_file")) log_file = cfg["log_file"].get<std::string>();
        if (cfg.contains("log_level")) log_level = cfg["log_level"].get<std::string>();
        if (cfg.contains("log_max_size_mb")) log_max_size_mb = cfg["log_max_size_mb"].get<int>();
        if (cfg.contains("log_backup_count")) log_backup_count = cfg["log_backup_count"].get<int>();
        if (cfg.contains("work_dir")) work_dir = cfg["work_dir"].get<std::string>();
        if (cfg.contains("state_file")) state_file = cfg["state_file"].get<std::string>();
        if (cfg.contains("max_request_size_kb")) max_request_size_kb = cfg["max_request_size_kb"].get<int>();
        if (cfg.contains("ollama_url")) ollama_url = cfg["ollama_url"].get<std::string>();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool Config::validate() const {
    return worker_count > 0 && poll_interval_ms > 0 && default_timeout_sec > 0;
}
