/*
 * logging.hpp - Controllable logging system for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <string>
#include <iostream>

namespace picexplore {

/**
 * Debug logging categories
 */
enum class LogCategory {
    BATCH,    ///< Image batch and layout processing
    UI,       ///< User interface and layout operations
    THREAD,   ///< Thread management and thumbnail processing
    SCAN      ///< Directory scanning and file discovery
};

/**
 * Debug logging levels
 */
enum class LogLevel {
    NONE = 0,   ///< No logging
    BASIC = 1,  ///< Basic triggers and summary
    VERBOSE = 2 ///< Verbose (all detailed queueing, batch, incremental updates)
};

/**
 * Controllable logging utility that checks environment variables for 
 * category and level control.
 * 
 * Environment variables:
 * - BATCH_LOGGING: Controls batch processing logging (0, 1, or 2)
 * - UI_LOGGING: Controls UI and layout logging (0, 1, or 2)
 * - THREAD_LOGGING: Controls thread management logging (0, 1, or 2)
 * - SCAN_LOGGING: Controls scanning logging (0, 1, or 2)
 * - PICEXPLORE_LOGGING: Global logging level override (0, 1, or 2)
 */
class Logger {
public:
    /**
     * Get the singleton logger instance
     */
    static Logger& getInstance();

    /**
     * Check if logging is enabled for a category and level
     */
    bool isEnabled(LogCategory category, LogLevel level) const;

    /**
     * Log a message for a specific category and level
     */
    void log(LogCategory category, LogLevel level, const std::string& message) const;

    /**
     * Convenience methods for specific categories
     */
    void logBatch(LogLevel level, const std::string& message) const;
    void logUI(LogLevel level, const std::string& message) const;
    void logThread(LogLevel level, const std::string& message) const;
    void logScan(LogLevel level, const std::string& message) const;

private:
    Logger();
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /**
     * Get the log level for a category from environment variables
     */
    LogLevel getCategoryLevel(LogCategory category) const;

    /**
     * Get category name as string
     */
    const char* getCategoryName(LogCategory category) const;

    /**
     * Cache for log levels (initialized once)
     */
    mutable LogLevel batch_level_;
    mutable LogLevel ui_level_;
    mutable LogLevel thread_level_;
    mutable LogLevel scan_level_;
    mutable LogLevel global_level_;
    mutable bool initialized_;

    /**
     * Initialize log levels from environment variables
     */
    void initializeLevels() const;
};

} // namespace picexplore

/**
 * Convenience macros for logging
 */
#define LOG_BATCH_BASIC(msg) \
    picexplore::Logger::getInstance().logBatch(picexplore::LogLevel::BASIC, msg)

#define LOG_BATCH_VERBOSE(msg) \
    picexplore::Logger::getInstance().logBatch(picexplore::LogLevel::VERBOSE, msg)

#define LOG_UI_BASIC(msg) \
    picexplore::Logger::getInstance().logUI(picexplore::LogLevel::BASIC, msg)

#define LOG_UI_VERBOSE(msg) \
    picexplore::Logger::getInstance().logUI(picexplore::LogLevel::VERBOSE, msg)

#define LOG_THREAD_BASIC(msg) \
    picexplore::Logger::getInstance().logThread(picexplore::LogLevel::BASIC, msg)

#define LOG_THREAD_VERBOSE(msg) \
    picexplore::Logger::getInstance().logThread(picexplore::LogLevel::VERBOSE, msg)

#define LOG_SCAN_BASIC(msg) \
    picexplore::Logger::getInstance().logScan(picexplore::LogLevel::BASIC, msg)

#define LOG_SCAN_VERBOSE(msg) \
    picexplore::Logger::getInstance().logScan(picexplore::LogLevel::VERBOSE, msg)
