#include "StateFile.h"
#include "Logger.h"
#include <fstream>
#include <windows.h>
#include <ctime>
#include <chrono>
#include <nlohmann/json.hpp>

StateFile::StateFile(const std::string& work_dir, const std::string& state_file)
    : path_(work_dir + "\\" + state_file) {
    state.pid = static_cast<int>(GetCurrentProcessId());
    updateStartTime();
}

void StateFile::updateStartTime() {
    char buf[64];
    time_t now = time(nullptr);
    struct tm tm_buf;
    gmtime_s(&tm_buf, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    start_time_ = buf;
    start_time_t_ = now;
}

void StateFile::setRunning(bool running) {
    state.status = running ? "running" : "stopped";
    write();
}

void StateFile::update(int busy_workers, int queue_length,
                       const std::vector<std::string>& clients,
                       uint64_t total_requests, uint64_t errors) {
    state.busy_workers = busy_workers;
    state.idle_workers = state.total_workers - busy_workers;
    state.queue_length = queue_length;
    state.clients = clients;

    time_t now = time(nullptr);
    state.uptime_seconds = static_cast<int>(now - start_time_t_);

    state.total_requests = total_requests;
    state.error_requests = errors;
    // Prevent underflow: ok_requests cannot be negative
    state.ok_requests = (errors >= total_requests) ? 0 : (total_requests - errors);

    write();
}

void StateFile::write() {
    nlohmann::json j;
    j["version"] = state.version;
    j["status"] = state.status;
    j["pid"] = state.pid;
    j["start_time"] = start_time_;
    j["uptime_seconds"] = state.uptime_seconds;
    j["workers"]["total"] = state.total_workers;
    j["workers"]["busy"] = state.busy_workers;
    j["workers"]["idle"] = state.idle_workers;
    j["queue_length"] = state.queue_length;
    j["clients"] = state.clients;
    j["stats"]["total_requests"] = state.total_requests;
    j["stats"]["ok_requests"] = state.ok_requests;
    j["stats"]["error_requests"] = state.error_requests;
    j["config"]["poll_interval_ms"] = state.poll_interval_ms;
    j["config"]["worker_count"] = state.worker_count;
    j["config"]["default_timeout_sec"] = state.default_timeout_sec;

    std::string tmp = path_ + ".tmp";
    std::ofstream of(tmp);
    if (!of.is_open()) return;
    of << j.dump(2);
    of.close();

    // Atomic rename
    std::wstring wsrc(tmp.begin(), tmp.end());
    std::wstring wdst(path_.begin(), path_.end());
    MoveFileExW(wsrc.c_str(), wdst.c_str(), MOVEFILE_REPLACE_EXISTING);
}
