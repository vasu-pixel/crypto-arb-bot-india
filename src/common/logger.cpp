#include "common/logger.h"

std::shared_ptr<spdlog::logger> Logger::logger_;

void Logger::init(const std::string& level, const std::string& log_file) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    if (!log_file.empty()) {
        sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_file, 10 * 1024 * 1024, 5));
    }

    logger_ = std::make_shared<spdlog::logger>("arb_bot", sinks.begin(), sinks.end());
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

    if (level == "trace") logger_->set_level(spdlog::level::trace);
    else if (level == "debug") logger_->set_level(spdlog::level::debug);
    else if (level == "info") logger_->set_level(spdlog::level::info);
    else if (level == "warn") logger_->set_level(spdlog::level::warn);
    else if (level == "error") logger_->set_level(spdlog::level::err);
    else logger_->set_level(spdlog::level::info);

    spdlog::set_default_logger(logger_);
    LOG_INFO("Logger initialized at level: {}", level);
}

std::shared_ptr<spdlog::logger> Logger::get() {
    if (!logger_) {
        logger_ = spdlog::stdout_color_mt("arb_bot");
        logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    }
    return logger_;
}
