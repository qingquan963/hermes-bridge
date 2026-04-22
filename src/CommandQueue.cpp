#include "CommandQueue.h"

void CommandQueue::enqueue(Command&& cmd) {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        q_.push(std::move(cmd));
    }
    cv_.notify_one();
}

Command CommandQueue::dequeue() {
    std::unique_lock<std::mutex> lock(mtx_);
    cv_.wait(lock, [this] { return !q_.empty() || shutting_down_.load(); });
    if (shutting_down_.load() && q_.empty()) {
        return Command{}; // empty command signals shutdown
    }
    Command cmd = std::move(q_.front());
    q_.pop();
    return cmd;
}

bool CommandQueue::dequeueFor(int ms) {
    std::unique_lock<std::mutex> lock(mtx_);
    auto wait_result = cv_.wait_for(lock, std::chrono::milliseconds(ms),
        [this] { return !q_.empty() || shutting_down_.load(); });
    if (!wait_result) return false; // timeout
    if (shutting_down_.load() && q_.empty()) return false;
    return true;
}

bool CommandQueue::empty() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return q_.empty();
}

size_t CommandQueue::size() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return q_.size();
}

void CommandQueue::wakeAll() {
    shutting_down_.store(true);
    cv_.notify_all();
}
