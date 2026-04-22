#pragma once
#ifndef SIMPLE_LOGGER_H
#define SIMPLE_LOGGER_H

#include <string>
#include <memory>
#include <mutex>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
#include <algorithm>

class RotatingFileLogger {
public:
    RotatingFileLogger(const std::string& log_path,
                       size_t max_size_bytes,
                       int backup_count,
                       const std::string& level)
        : log_path_(log_path)
        , max_size_bytes_(max_size_bytes)
        , backup_count_(backup_count)
        , level_(level)
    {
        openLog();
    }

    void set_pattern(const std::string&) { /* no-op, pattern is fixed */ }

    void set_level(const std::string& level) { level_ = level; }

    void set_flush_on(const std::string& level) {
        if (level == "info" || level == "debug" || level == "warn" || level == "error")
            flush_level_ = level;
    }

    void info(const std::string& msg) { log("INFO", msg); }
    void warn(const std::string& msg) { log("WARN", msg); }
    void error(const std::string& msg) { log("ERROR", msg); }
    void debug(const std::string& msg) { log("DEBUG", msg); }

private:
    void openLog() {
        std::lock_guard<std::mutex> lock(mutex_);
        log_file_.open(log_path_, std::ios::app | std::ios::binary);
    }

    std::string format_time() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        char buf[32];
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
            tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
            static_cast<int>(ms.count()));
        return std::string(buf);
    }

    void log(const std::string& level_str, const std::string& msg) {
        if (level_ == "debug" && level_str == "DEBUG") { /* skip */ }
        else if (level_ == "warn" && (level_str == "INFO" || level_str == "DEBUG")) { /* skip */ }
        else if (level_ == "error" && (level_str == "INFO" || level_str == "DEBUG" || level_str == "WARN")) { /* skip */ }

        std::lock_guard<std::mutex> lock(mutex_);

        if (!log_file_.is_open()) return;

        std::ostringstream oss;
        oss << "[" << format_time() << "] [" << level_str << "] " << msg << "\n";

        std::string line = oss.str();

        // Check rotation
        auto pos = log_file_.tellp();
        if (pos > 0 && static_cast<size_t>(pos) + line.size() > max_size_bytes_) {
            log_file_.close();
            rotate();
            log_file_.open(log_path_, std::ios::app | std::ios::binary);
        }

        log_file_ << line;
        log_file_.flush();
    }

    void rotate() {
        // Delete oldest
        std::string oldest = log_path_ + "." + std::to_string(backup_count_) + ".bak";
        RemoveFile(oldest);

        // Shift others
        for (int i = backup_count_ - 1; i >= 1; --i) {
            std::string src = log_path_ + "." + std::to_string(i) + ".bak";
            std::string dst = log_path_ + "." + std::to_string(i + 1) + ".bak";
            rename(src.c_str(), dst.c_str());
        }

        // Rename current to .1.bak
        std::string first = log_path_ + ".1.bak";
        rename(log_path_.c_str(), first.c_str());
    }

    static void RemoveFile(const std::string& path) {
        remove(path.c_str());
    }

    std::string log_path_;
    size_t max_size_bytes_;
    int backup_count_;
    std::string level_ = "info";
    std::string flush_level_ = "info";
    std::ofstream log_file_;
    std::mutex mutex_;
};

#endif // SIMPLE_LOGGER_H
