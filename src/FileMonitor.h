#pragma once
#ifndef HERMES_BRIDGE_FILEMONITOR_H
#define HERMES_BRIDGE_FILEMONITOR_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include "CommandQueue.h"
#include "Config.h"

class FileMonitor {
public:
    FileMonitor(const Config& config, CommandQueue& queue);
    ~FileMonitor();
    void start();
    void stop();
    std::vector<std::string> discoveredClients() const;
    std::string lastError() const { return lastError_; }

private:
    void pollLoop();
    bool scanAndEnqueue();
    bool isJsonComplete(const std::string& content) const;
    std::string readCmdFile(const std::string& path) const;
    bool truncateFile(const std::string& path) const;

    const Config& config_;
    CommandQueue& queue_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> running_{false};
    mutable std::mutex clients_mtx_;
    std::unordered_set<std::string> clients_;
    std::string lastError_;

    // Bug 1 fix: cmd_id deduplication cache (per-client)
    mutable std::mutex seen_mtx_;
    std::unordered_map<std::string, std::unordered_set<std::string>> seen_cmd_ids_;  // client_id -> set of cmd_ids
};

#endif // HERMES_BRIDGE_FILEMONITOR_H
