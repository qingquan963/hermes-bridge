#pragma once
#ifndef HERMES_BRIDGE_STATEFILE_H
#define HERMES_BRIDGE_STATEFILE_H

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <atomic>
#include <ctime>

class StateFile {
public:
    StateFile(const std::string& work_dir, const std::string& state_file);

    void update(int busy_workers, int queue_length,
                const std::vector<std::string>& clients,
                uint64_t total_requests, uint64_t errors);
    void setRunning(bool running);
    void write();

    struct State {
        std::string version = "1.0.0";
        std::string status = "starting";
        int pid = 0;
        int total_workers = 5;
        int busy_workers = 0;
        int idle_workers = 0;
        int queue_length = 0;
        std::vector<std::string> clients;
        uint64_t total_requests = 0;
        uint64_t ok_requests = 0;
        uint64_t error_requests = 0;
        int poll_interval_ms = 5000;
        int worker_count = 5;
        int default_timeout_sec = 30;
        int uptime_seconds = 0;
    };

    State state;

private:
    std::string path_;
    std::string start_time_;
    time_t start_time_t_;
    void updateStartTime();
};

#endif // HERMES_BRIDGE_STATEFILE_H
