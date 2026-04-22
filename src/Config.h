#pragma once
#ifndef HERMES_BRIDGE_CONFIG_H
#define HERMES_BRIDGE_CONFIG_H

#include <string>
#include <cstdint>

struct Config {
    std::string version = "1.0.0";
    int poll_interval_ms = 5000;
    int worker_count = 5;
    int default_timeout_sec = 30;
    std::string log_dir = "logs";
    std::string log_file = "events.txt";
    std::string log_level = "info";
    int log_max_size_mb = 10;
    int log_backup_count = 5;
    std::string work_dir = "C:\\lobster\\hermes_bridge";
    std::string state_file = "state.json";
    int max_request_size_kb = 10240;
    std::string ollama_url = "http://127.0.0.1:11434/api/generate";

    bool load(const std::string& path);
    bool validate() const;
};

#endif // HERMES_BRIDGE_CONFIG_H
