#include "ThreadPool.h"

ThreadPool::ThreadPool(int nWorkers) : nWorkers_(nWorkers) {
    // Workers are started externally in main.cpp
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::shutdown() {
    stop_.store(true);
    // Workers join via the cv in CommandQueue wakeAll
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

int ThreadPool::idleCount() const {
    return nWorkers_;
}

void ThreadPool::setBusy(bool b) {
    if (b) busy_++;
    else if (busy_ > 0) busy_--;
}
