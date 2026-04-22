#pragma once
#ifndef HERMES_BRIDGE_COMMANDQUEUE_H
#define HERMES_BRIDGE_COMMANDQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <atomic>

struct Command {
    std::string cmd_id;
    std::string action;
    nlohmann::json params;
    int timeout = 30;
    std::string client_id;
    std::string timestamp;
    bool force = false;  // if true, bypass any cmd_id deduplication cache
    std::string cmd_callback_url;  // empty = no callback
};

class CommandQueue {
public:
    void enqueue(Command&& cmd);
    Command dequeue();
    bool dequeueFor(int ms); // returns false on timeout
    bool empty() const;
    size_t size() const;
    void wakeAll(); // wakes all waiters (for shutdown)
    bool isShuttingDown() const { return shutting_down_.load(); }

private:
    std::queue<Command> q_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> shutting_down_{false};
};

#endif // HERMES_BRIDGE_COMMANDQUEUE_H
