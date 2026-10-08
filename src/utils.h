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
#include <cstdint>
#include <atomic>

// Filesystem identity and freshness belong to a path, not to shared image content.
struct FileStamp {
    uint64_t device = 0, inode = 0, size = 0;
    int64_t modified_ns = 0, changed_ns = 0;
    bool valid = false;
    bool operator==(const FileStamp& other) const {
        return valid && other.valid && device == other.device && inode == other.inode &&
            size == other.size && modified_ns == other.modified_ns && changed_ns == other.changed_ns;
    }
    bool operator!=(const FileStamp& other) const { return !(*this == other); }
};
bool read_file_stamp(const std::string& path, FileStamp& stamp);
std::string normalize_path(const std::string& path);
bool path_is_within(const std::string& path, const std::string& directory);

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

    private:
	void report_thread();

	int interval_seconds_;
	std::string current_status_;
	int total_count_;
	int current_count_;
	bool running_;
	std::thread* reporter_thread_;
	std::mutex status_mutex_;
};

// Utility functions
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality = 90);
bool is_image_file(const std::string& filepath);
bool is_cache_or_db_path(const std::string& path, const std::string& db_path = "");
bool get_image_info(const std::string& filepath, int* width, int* height);
bool load_webp_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h, int* orig_w = nullptr, int* orig_h = nullptr);
bool load_tiff_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h, int* orig_w = nullptr, int* orig_h = nullptr);
bool load_png_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h, int* orig_w = nullptr, int* orig_h = nullptr, const std::atomic<bool>* cancelled = nullptr);
bool compute_file_hash(const std::string& filepath, std::string& hash_out, const std::atomic<bool>* cancelled = nullptr);

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s

// All consumers share identification, orientation, and bounded decode policy.
enum class ImageFormat { UNKNOWN, JPEG, PNG, WEBP, TIFF, OTHER };
struct ImageProbe {
    ImageFormat format = ImageFormat::UNKNOWN;
    int width = 0, height = 0, orientation = 1;
    bool supports_tiles = false;
    std::string error;
    explicit operator bool() const { return width > 0 && height > 0 && error.empty(); }
};
enum class DecodeStatus { READY, FAILED, CANCELLED, TOO_LARGE, SOURCE_CHANGED };
struct DecodeResult {
    DecodeStatus status = DecodeStatus::FAILED;
    ImageProbe source;
    std::vector<uint8_t> rgb;
    int width = 0, height = 0;
    std::string error;
    explicit operator bool() const { return status == DecodeStatus::READY; }
};
ImageProbe probe_image(const std::string& path);
DecodeResult decode_image(const std::string& path, int max_width = 0, int max_height = 0,
                          const std::atomic<bool>* cancelled = nullptr);
void orient_rgb(std::vector<uint8_t>& rgb, int& width, int& height, int orientation);
bool load_jpeg_scaled(const std::string& path, int max_w, int max_h, std::vector<uint8_t>& rgb,
                      int& width, int& height, int* orig_w = nullptr, int* orig_h = nullptr);
