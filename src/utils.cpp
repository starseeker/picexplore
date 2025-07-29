/*
 * utils.cpp - Utility functions for picscan
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

#include "utils.hpp"
#include <thread>
#include <mutex>
#include <algorithm>
#include <filesystem>
#include <cstdlib>

#include "stb_image_write.h"

// Timer implementation
void Timer::start(const std::string& phase) {
    active_timers_[phase] = std::chrono::high_resolution_clock::now();
}

void Timer::stop(const std::string& phase) {
    auto it = active_timers_.find(phase);
    if (it != active_timers_.end()) {
	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - it->second);
	phase_times_[phase] += duration;
	active_timers_.erase(it);
    }
}

void Timer::print_summary() const {
    if (phase_times_.empty()) {
	std::cout << "No timing data recorded." << std::endl;
	return;
    }

    std::cout << "\n=== Timing Summary ===" << std::endl;
    std::cout << std::left << std::setw(25) << "Phase" << "Time (seconds)" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    auto total_time = std::chrono::milliseconds(0);
    for (const auto& entry : phase_times_) {
	total_time += entry.second;
    }

    for (const auto& entry : phase_times_) {
	double seconds = entry.second.count() / 1000.0;
	double percentage = (total_time.count() > 0) ? (entry.second.count() * 100.0 / total_time.count()) : 0.0;
	std::cout << std::left << std::setw(25) << entry.first
	    << std::fixed << std::setprecision(3) << seconds
	    << " (" << std::setprecision(1) << percentage << "%)" << std::endl;
    }

    std::cout << std::string(40, '-') << std::endl;
    std::cout << std::left << std::setw(25) << "TOTAL"
	<< std::fixed << std::setprecision(3) << (total_time.count() / 1000.0) << std::endl;
}

// StatusReporter implementation
StatusReporter::StatusReporter(int interval_seconds)
    : interval_seconds_(interval_seconds), current_status_("Initializing..."),
    total_count_(0), current_count_(0), running_(false), completed_(false), reporter_thread_(nullptr) {
    }

StatusReporter::~StatusReporter() {
    stop();
}

void StatusReporter::update_status(const std::string& message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_status_ = message;
}

void StatusReporter::set_total_count(int total) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    total_count_ = total;
}

void StatusReporter::set_current_count(int current) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_count_ = current;
}

void StatusReporter::start() {
    if (running_) return;

    running_ = true;
    reporter_thread_ = new std::thread(&StatusReporter::report_thread, this);
}

void StatusReporter::stop() {
    if (!running_) return;

    running_ = false;
    if (reporter_thread_) {
	reporter_thread_->join();
	delete reporter_thread_;
	reporter_thread_ = nullptr;
    }
}

void StatusReporter::mark_complete() {
    {
	std::lock_guard<std::mutex> lock(status_mutex_);
	completed_ = true;
    }
    stop();  // Stop the reporting thread
}

void StatusReporter::report_thread() {
    while (running_) {
	std::this_thread::sleep_for(std::chrono::seconds(interval_seconds_));

	if (!running_) break;

	std::lock_guard<std::mutex> lock(status_mutex_);

	// Don't print if we're completed
	if (completed_) break;

	std::cout << "[STATUS] " << current_status_;
	if (total_count_ > 0) {
	    std::cout << " (" << current_count_ << "/" << total_count_ << " - "
		<< std::fixed << std::setprecision(1)
		<< (current_count_ * 100.0 / total_count_) << "%)";
	}
	std::cout << std::endl;
    }
}

// Utility functions
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality) {
    std::vector<uint8_t> jpeg_data;

    // STB write callback to capture data
    auto write_func = [](void* context, void* data, int size) {
	auto* vec = static_cast<std::vector<uint8_t>*>(context);
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	vec->insert(vec->end(), bytes, bytes + size);
    };

    if (stbi_write_jpg_to_func(write_func, &jpeg_data, width, height, 3, rgb_data, quality)) {
	return jpeg_data;
    }

    return {}; // Empty vector on failure
}

bool is_image_file(const std::string& filepath) {
    auto ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".tga");
}

std::string get_cache_db_path(bool silent) {
    std::filesystem::path cache_dir;

#ifdef _WIN32
    // Windows: %LOCALAPPDATA%\picexplore\picexplore.db
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata) {
	cache_dir = std::filesystem::path(localappdata) / "picexplore";
    } else {
	// Fallback to %USERPROFILE%\AppData\Local\picexplore
	const char* userprofile = std::getenv("USERPROFILE");
	if (userprofile) {
	    cache_dir = std::filesystem::path(userprofile) / "AppData" / "Local" / "picexplore";
	} else {
	    // Final fallback to current directory
	    cache_dir = std::filesystem::current_path() / "picexplore_cache";
	}
    }
#elif defined(__APPLE__)
    // macOS: $HOME/Library/Caches/picexplore/picexplore.db
    const char* home = std::getenv("HOME");
    if (home) {
	cache_dir = std::filesystem::path(home) / "Library" / "Caches" / "picexplore";
    } else {
	// Fallback to current directory
	cache_dir = std::filesystem::current_path() / "picexplore_cache";
    }
#else
    // Linux and other Unix-like systems: ~/.cache/picexplore/picexplore.db
    const char* home = std::getenv("HOME");
    if (home) {
	cache_dir = std::filesystem::path(home) / ".cache" / "picexplore";
    } else {
	// Fallback to current directory
	cache_dir = std::filesystem::current_path() / "picexplore_cache";
    }
#endif

    // Ensure the cache directory exists
    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
	// Use current directory as fallback
	cache_dir = std::filesystem::current_path();
    }

    std::filesystem::path db_path = cache_dir / "picexplore.db";

    return db_path.string();
}
// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
