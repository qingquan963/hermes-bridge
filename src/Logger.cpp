#include "Logger.h"
#include <filesystem>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::string& log_dir, const std::string& log_file,
                  const std::string& level, int max_size_mb, int backup_count) {
    namespace fs = std::filesystem;
    fs::path dir(log_dir);
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
    }

    fs::path log_path = dir / log_file;

    logger_ = std::make_unique<RotatingFileLogger>(
        log_path.string(),
        1024ULL * 1024ULL * static_cast<size_t>(max_size_mb),
        backup_count,
        level);

    if (level == "debug")
        logger_->set_level("debug");
    else if (level == "warn")
        logger_->set_level("warn");
    else if (level == "error")
        logger_->set_level("error");
    else
        logger_->set_level("info");

    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    logger_->set_flush_on("info");
}

void Logger::info(const std::string& msg) { logger_->info(msg); }
void Logger::warn(const std::string& msg) { logger_->warn(msg); }
void Logger::error(const std::string& msg) { logger_->error(msg); }
void Logger::debug(const std::string& msg) { logger_->debug(msg); }
