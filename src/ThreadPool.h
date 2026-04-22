#pragma once
#ifndef HERMES_BRIDGE_THREADPOOL_H
#define HERMES_BRIDGE_THREADPOOL_H

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// ThreadPool manages the worker threads.
// Workers are started in main.cpp and pull from CommandQueue directly.
// ThreadPool provides shutdown and busy/idle state tracking.
class ThreadPool {
public:
    explicit ThreadPool(int nWorkers);
    ~ThreadPool();
    void shutdown();
    int idleCount() const;
    int busyCount() const { return busy_.load(); }
    int totalCount() const { return nWorkers_; }

    // Called by workers to report busy/idle state
    void setBusy(bool b);

private:
    std::vector<std::thread> threads_;
    std::atomic<bool> stop_{false};
    std::atomic<int> busy_{0};
    int nWorkers_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

#endif // HERMES_BRIDGE_THREADPOOL_H
