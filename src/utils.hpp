/*
 * utils.h - Utility functions for picscan
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

#include <chrono>
#include <string>
#include <map>
#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <vector>

// Timer class for measuring execution time of different phases
class Timer {
    public:
	void start(const std::string& phase);
	void stop(const std::string& phase);
	void print_summary() const;

    private:
	std::map<std::string, std::chrono::milliseconds> phase_times_;
	std::map<std::string, std::chrono::high_resolution_clock::time_point> active_timers_;
};

// Status reporter for periodic updates
class StatusReporter {
    public:
	StatusReporter(int interval_seconds = 10);
	~StatusReporter();

	void update_status(const std::string& message);
	void set_total_count(int total);
	void set_current_count(int current);
	void start();
	void stop();
	void mark_complete();  // Stop reporting and mark as completed

    private:
	void report_thread();

	int interval_seconds_;
	std::string current_status_;
	int total_count_;
	int current_count_;
	bool running_;
	bool completed_;  // Track completion state
	std::thread* reporter_thread_;
	std::mutex status_mutex_;
};

// Utility functions
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality = 90);
bool is_image_file(const std::string& filepath);

// Thumbnail size utilities

/**
 * Selects the most appropriate canonical thumbnail size for a given display size.
 * Maps any requested size to one of the canonical sizes {32, 64, 128, 256, 512, 1024}
 * to ensure cache consistency and prevent cache misses due to exact size mismatches.
 */
int pick_thumbnail_size(int requested_width, int requested_height);

// Thumbnail key utilities
std::string make_thumbnail_key(const std::string& hash, int size);
std::string make_thumbnail_key(const std::string& hash, int width, int height);

// Cross-platform cache path management
std::string get_cache_db_path(bool silent = false);
// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
