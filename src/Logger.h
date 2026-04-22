#pragma once
#ifndef HERMES_BRIDGE_LOGGER_H
#define HERMES_BRIDGE_LOGGER_H

#include <string>
#include <memory>
#include <sstream>
#include "RotatingFileLogger.h"

class Logger {
public:
    static Logger& instance();
    void init(const std::string& log_dir, const std::string& log_file,
              const std::string& level, int max_size_mb, int backup_count);
    RotatingFileLogger& logger() { return *logger_; }
    void info(const std::string& msg);
    void warn(const std::string& msg);
    void error(const std::string& msg);
    void debug(const std::string& msg);

private:
    Logger() = default;
    std::unique_ptr<RotatingFileLogger> logger_;
};

// --- Variadic log_format implementation ---
namespace logfmt {

// Base case: no args left
inline void build_args(std::vector<std::string>&) {}

// Recursive case: accumulate next arg
template<typename T, typename... Args>
void build_args(std::vector<std::string>& out, T&& val, Args&&... rest) {
    std::ostringstream ss;
    ss << val;
    out.push_back(ss.str());
    build_args(out, std::forward<Args>(rest)...);
}

template<size_t N>
struct ArgArray {
    const char* fmt;
    std::vector<std::string> args;

    template<typename... Args>
    ArgArray(const char* f, Args&&... a) : fmt(f) {
        args.reserve(sizeof...(Args));
        build_args(args, std::forward<Args>(a)...);
    }
};

// Format with {} replacement
template<typename... Args>
std::string format_impl(const char* fmt, Args&&... args) {
    std::vector<std::string> arg_vec;
    arg_vec.reserve(sizeof...(Args));
    build_args(arg_vec, std::forward<Args>(args)...);

    std::ostringstream result;
    const char* p = fmt;
    size_t arg_idx = 0;

    while (*p) {
        if (p[0] == '{' && p[1] == '}') {
            if (arg_idx < arg_vec.size())
                result << arg_vec[arg_idx];
            ++arg_idx;
            p += 2;
        } else if (p[0] == '{' && p[1] == '{') {
            result << '{';
            p += 2;
        } else if (p[0] == '}' && p[1] == '}') {
            result << '}';
            p += 2;
        } else {
            result << *p++;
        }
    }
    return result.str();
}

} // namespace logfmt

// log_format: single string arg -> passthrough
inline std::string log_format(const std::string& s) { return s; }
// log_format: single const char* arg -> passthrough
inline std::string log_format(const char* s) { return std::string(s); }
// log_format: variadic args -> format with replacement
template<typename... Args>
std::string log_format(const char* fmt, Args&&... args) {
    return logfmt::format_impl(fmt, std::forward<Args>(args)...);
}

#define LOG_INFO(...) Logger::instance().info(log_format(__VA_ARGS__))
#define LOG_WARN(...) Logger::instance().warn(log_format(__VA_ARGS__))
#define LOG_ERROR(...) Logger::instance().error(log_format(__VA_ARGS__))
#define LOG_DEBUG(...) Logger::instance().debug(log_format(__VA_ARGS__))

#endif // HERMES_BRIDGE_LOGGER_H
