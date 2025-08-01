/*
 * logging.cpp - Implementation of controllable logging system for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "logging.hpp"
#include <cstdlib>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

namespace picexplore {

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

Logger::Logger() 
    : batch_level_(LogLevel::NONE)
    , ui_level_(LogLevel::NONE)
    , thread_level_(LogLevel::NONE)
    , scan_level_(LogLevel::NONE)
    , thumbnail_level_(LogLevel::NONE)
    , global_level_(LogLevel::NONE)
    , image_filter_("")
    , initialized_(false) {
}

void Logger::initializeLevels() const {
    if (initialized_) {
        return;
    }

    // Check global override first
    const char* global_env = std::getenv("PICEXPLORE_LOGGING");
    if (global_env) {
        int level = std::atoi(global_env);
        global_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        global_level_ = LogLevel::NONE;
    }

    // Check category-specific levels
    const char* batch_env = std::getenv("BATCH_LOGGING");
    if (batch_env) {
        int level = std::atoi(batch_env);
        batch_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        batch_level_ = global_level_;
    }

    const char* ui_env = std::getenv("UI_LOGGING");
    if (ui_env) {
        int level = std::atoi(ui_env);
        ui_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        ui_level_ = global_level_;
    }

    const char* thread_env = std::getenv("THREAD_LOGGING");
    if (thread_env) {
        int level = std::atoi(thread_env);
        thread_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        thread_level_ = global_level_;
    }

    const char* scan_env = std::getenv("SCAN_LOGGING");
    if (scan_env) {
        int level = std::atoi(scan_env);
        scan_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        scan_level_ = global_level_;
    }

    const char* thumbnail_env = std::getenv("THUMBNAIL_LOGGING");
    if (thumbnail_env) {
        int level = std::atoi(thumbnail_env);
        thumbnail_level_ = static_cast<LogLevel>(std::max(0, std::min(2, level)));
    } else {
        thumbnail_level_ = global_level_;
    }

    // Check image filter
    const char* image_filter_env = std::getenv("THUMBNAIL_LOG_IMAGE");
    if (image_filter_env) {
        image_filter_ = std::string(image_filter_env);
    } else {
        image_filter_ = "";
    }

    initialized_ = true;
}

LogLevel Logger::getCategoryLevel(LogCategory category) const {
    if (!initialized_) {
        initializeLevels();
    }

    switch (category) {
        case LogCategory::BATCH:
            return batch_level_;
        case LogCategory::UI:
            return ui_level_;
        case LogCategory::THREAD:
            return thread_level_;
        case LogCategory::SCAN:
            return scan_level_;
        case LogCategory::THUMBNAIL:
            return thumbnail_level_;
        default:
            return LogLevel::NONE;
    }
}

const char* Logger::getCategoryName(LogCategory category) const {
    switch (category) {
        case LogCategory::BATCH:
            return "BATCH";
        case LogCategory::UI:
            return "UI";
        case LogCategory::THREAD:
            return "THREAD";
        case LogCategory::SCAN:
            return "SCAN";
        case LogCategory::THUMBNAIL:
            return "THUMBNAIL";
        default:
            return "UNKNOWN";
    }
}

bool Logger::isEnabled(LogCategory category, LogLevel level) const {
    LogLevel category_level = getCategoryLevel(category);
    return static_cast<int>(level) <= static_cast<int>(category_level);
}

void Logger::log(LogCategory category, LogLevel level, const std::string& message) const {
    if (!isEnabled(category, level)) {
        return;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t_now), "%H:%M:%S");
    timestamp << "." << std::setfill('0') << std::setw(3) << ms.count();

    // Format: [HH:MM:SS.mmm] [CATEGORY:LEVEL] message
    const char* level_str = (level == LogLevel::BASIC) ? "1" : "2";
    std::cerr << "[" << timestamp.str() << "] [" 
              << getCategoryName(category) << ":" << level_str << "] " 
              << message << std::endl;
}

void Logger::log(LogCategory category, LogLevel level, const std::string& message, const std::string& image_path) const {
    if (!isEnabled(category, level)) {
        return;
    }

    // Check image filter if set
    if (!matchesImageFilter(image_path)) {
        return;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream timestamp;
    timestamp << std::put_time(std::localtime(&time_t_now), "%H:%M:%S");
    timestamp << "." << std::setfill('0') << std::setw(3) << ms.count();

    // Format: [HH:MM:SS.mmm] [CATEGORY:LEVEL] message
    const char* level_str = (level == LogLevel::BASIC) ? "1" : "2";
    std::cerr << "[" << timestamp.str() << "] [" 
              << getCategoryName(category) << ":" << level_str << "] " 
              << message << std::endl;
}

void Logger::logBatch(LogLevel level, const std::string& message) const {
    log(LogCategory::BATCH, level, message);
}

void Logger::logUI(LogLevel level, const std::string& message) const {
    log(LogCategory::UI, level, message);
}

void Logger::logThread(LogLevel level, const std::string& message) const {
    log(LogCategory::THREAD, level, message);
}

void Logger::logScan(LogLevel level, const std::string& message) const {
    log(LogCategory::SCAN, level, message);
}

void Logger::logThumbnail(LogLevel level, const std::string& message) const {
    log(LogCategory::THUMBNAIL, level, message);
}

void Logger::logBatch(LogLevel level, const std::string& message, const std::string& image_path) const {
    log(LogCategory::BATCH, level, message, image_path);
}

void Logger::logUI(LogLevel level, const std::string& message, const std::string& image_path) const {
    log(LogCategory::UI, level, message, image_path);
}

void Logger::logThread(LogLevel level, const std::string& message, const std::string& image_path) const {
    log(LogCategory::THREAD, level, message, image_path);
}

void Logger::logScan(LogLevel level, const std::string& message, const std::string& image_path) const {
    log(LogCategory::SCAN, level, message, image_path);
}

void Logger::logThumbnail(LogLevel level, const std::string& message, const std::string& image_path) const {
    log(LogCategory::THUMBNAIL, level, message, image_path);
}

bool Logger::matchesImageFilter(const std::string& image_path) const {
    if (!initialized_) {
        initializeLevels();
    }

    // If no filter is set, always match
    if (image_filter_.empty()) {
        return true;
    }

    // If no image path provided, always match (fallback to normal logging)
    if (image_path.empty()) {
        return true;
    }

    // Extract filename and check if it matches the filter
    std::string filename = extractFilename(image_path);
    return filename == image_filter_;
}

std::string Logger::extractFilename(const std::string& path) const {
    if (path.empty()) {
        return "";
    }

    try {
        std::filesystem::path fs_path(path);
        return fs_path.filename().string();
    } catch (...) {
        // Fallback to simple string parsing if filesystem fails
        size_t last_slash = path.find_last_of("/\\");
        if (last_slash != std::string::npos && last_slash + 1 < path.length()) {
            return path.substr(last_slash + 1);
        }
        return path;
    }
}

} // namespace picexplore