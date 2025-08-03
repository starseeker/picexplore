/*
 * database.h - LMDB database and thumbnail operations for picscan
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
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <future>
#include "lmdb.h"
#include "utils.hpp"
#include "concurrentqueue.h"
#include "blockingconcurrentqueue.h"
#include "database_dal.hpp"

// Image information structure
struct ImageInfo {
    std::string path;
    std::string hash;
    double aspect_ratio = 1.0;
    int best_thumb_size = 0;
    std::vector<uint8_t> thumb_data;
    int thumb_width = 0;
    int thumb_height = 0;
    bool has_thumbnails = false;  // Track thumbnail availability for two-stage loading
};

// Write task for batch database operations
struct WriteTask {
    enum TaskType {
	STORE_PATH,
	STORE_THUMBNAIL,
	STORE_IMAGE_METADATA,  // Store metadata without thumbnails for stage 1
	SHUTDOWN  // Sentinel task for clean thread shutdown
    };

    TaskType type;
    std::string key;
    std::vector<uint8_t> data;
    std::string string_value; // For path storage

    // Metadata for early emission
    double aspect_ratio = 1.0;
    std::string file_path;

    WriteTask() = default;
    WriteTask(TaskType t, const std::string& k, const std::string& val)
	: type(t), key(k), string_value(val) {}

    WriteTask(TaskType t, const std::string& k, const std::vector<uint8_t>& d)
	: type(t), key(k), data(d) {}

    // Constructor for metadata storage
    WriteTask(TaskType t, const std::string& k, const std::string& path, double aspect)
	: type(t), key(k), file_path(path), aspect_ratio(aspect) {}
    
    // Create shutdown sentinel
    static WriteTask create_shutdown_sentinel() {
	return WriteTask(SHUTDOWN, "", "");
    }
};

// Database manager class
class DatabaseManager {
    public:
	DatabaseManager();
	~DatabaseManager();

	bool open(const std::string& db_path);
	void close();

	// Scanning and thumbnail generation
	int scan_directory(const std::string& directory, Timer& timer, StatusReporter& reporter);
	int scan_directory_parallel(const std::string& directory, Timer& timer, StatusReporter& reporter, int num_threads = 0);

	// Database querying
	std::vector<ImageInfo> get_all_images();
	std::vector<ImageInfo> get_images_since_count(size_t last_count);
	std::vector<ImageInfo> get_images_without_thumbnails(); // Get images that need thumbnails
	bool has_thumbnails(const std::string& hash);

	// Two-stage processing support
	using ImageInfoCallback = std::function<void(const ImageInfo&)>;
	void set_image_info_callback(ImageInfoCallback callback) { image_info_callback_ = callback; }

	// Stage 1: Extract image metadata (path, aspect ratio) quickly
	bool extract_image_metadata(const std::string& filepath, ImageInfo& info);

	// Stage 2: Generate thumbnails for existing image metadata
	bool generate_thumbnails_for_hash(const std::string& hash, const std::string& filepath);

	// Scanning control
	void cancel_scan();

	// Database operations (made public for UI access) - DEPRECATED, use DAL directly
	bool begin_write_transaction(MDB_txn*& txn);
	bool begin_read_transaction(MDB_txn*& txn);
	bool commit_transaction(MDB_txn* txn);
	void abort_transaction(MDB_txn* txn);
	bool load_image_info(MDB_txn* txn, const std::string& hash, ImageInfo& info);
	bool get_key_data(MDB_txn* txn, const std::string& key, std::vector<uint8_t>& data);

	// New DAL access for modern usage
	IDatabaseDAL* get_dal() { return dal_.get(); }
	const IDatabaseDAL* get_dal() const { return dal_.get(); }

    private:
	// Legacy LMDB fields - kept for backward compatibility of deprecated methods
	MDB_env* env_;
	MDB_dbi dbi_;
	bool is_open_;

	// New DAL interface
	std::unique_ptr<IDatabaseDAL> dal_;

	// Parallel processing
	mutable std::mutex db_mutex_;
	std::atomic<bool> stop_processing_;

	// Two-stage processing callback
	ImageInfoCallback image_info_callback_;

	// Worker thread functions
	void worker_thread(const std::vector<std::string>& files, size_t start_idx, size_t end_idx,
		moodycamel::BlockingConcurrentQueue<WriteTask>& write_queue,
		Timer& timer, StatusReporter& reporter,
		std::atomic<int>& processed_count, std::atomic<int>& skipped_count);

	// Writer thread function
	void writer_thread(moodycamel::BlockingConcurrentQueue<WriteTask>& write_queue,
		std::atomic<bool>& workers_done,
		Timer& timer, StatusReporter& reporter,
		std::atomic<int>& write_count);

	// Thumbnail generation
	bool generate_thumbnails(MDB_txn* txn, const std::string& filepath, const std::string& hash,
		unsigned char* image_data, int width, int height, int channels);
	std::vector<uint8_t> decode_jpeg_thumbnail_rgb(const std::string& filepath, int scale_factor,
		int* actual_width, int* actual_height);
	int calculate_scale_factor(int image_width, int image_height, int target_width, int target_height);

	// Database operations (remaining private methods)
	bool store_key_value(MDB_txn* txn, const std::string& key, const std::string& value);
	bool store_key_data(MDB_txn* txn, const std::string& key, const std::vector<uint8_t>& data);
	bool get_key_value(MDB_txn* txn, const std::string& key, std::string& value);
	std::string extract_hash_from_key(const char* key, size_t key_size);

	// Image processing helper
	bool process_image_file(const std::string& filepath,
		std::vector<WriteTask>& write_tasks,
		Timer& timer, bool& should_skip);

	// EXIF orientation helpers
	int get_exif_orientation(const std::string& filepath);
	void apply_orientation_transform(unsigned char* data, int& width, int& height, int orientation);
	
	// DAL-compatible helper methods
	bool load_image_info_from_dal(ITransaction& txn, const std::string& hash, ImageInfo& info);
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
