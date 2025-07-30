/*
 * thread_manager.cpp - New thread architecture implementation for picexplore
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

#include "thread_manager.hpp"
#include "utils.hpp"
#include <FL/Fl.H>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <cstring>
#include "stb_image.h"
#include "stb_image_resize2.h"

// Global flags initialization
std::atomic<bool> GlobalFlags::should_shutdown{false};
std::atomic<bool> GlobalFlags::should_cancel_scan{false};
std::atomic<bool> GlobalFlags::scanning_active{false};

//==============================================================================
// ProgressReporter Implementation
//==============================================================================

void ProgressReporter::report_progress(int current, int total, const std::string& status) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (progress_callback_) {
	progress_callback_(current, total, status);
    }
}

//==============================================================================
// DirectoryScanThread Implementation
//==============================================================================

DirectoryScanThread::DirectoryScanThread() : should_stop_(false) {
}

DirectoryScanThread::~DirectoryScanThread() {
    stop_scan();
    join();
}

bool DirectoryScanThread::start_scan(const std::string& directory_path, const std::string& db_path) {
    if (scan_thread_.joinable()) {
	// Already scanning
	return false;
    }

    directory_path_ = directory_path;
    db_path_ = db_path.empty() ? get_cache_db_path() : db_path;
    should_stop_.store(false);

    // Initialize database
    database_ = std::make_unique<DatabaseManager>();
    if (!database_->open(db_path_)) {
	std::cerr << "[ERROR] DirectoryScanThread: Failed to open database: " << db_path_ << std::endl;
	return false;
    }

    // Set up database callback for stage 1 metadata
    database_->set_image_info_callback([this](const ImageInfo& info) {
	if (metadata_callback_) {
	    metadata_callback_(info);
	}
    });

    GlobalFlags::set_scanning(true);
    scan_thread_ = std::thread(&DirectoryScanThread::scan_thread_main, this);

    return true;
}

void DirectoryScanThread::stop_scan() {
    should_stop_.store(true);
    if (database_) {
	database_->cancel_scan();
    }
}

void DirectoryScanThread::join() {
    if (scan_thread_.joinable()) {
	scan_thread_.join();
    }
    GlobalFlags::set_scanning(false);
}

void DirectoryScanThread::scan_thread_main() {
    std::cout << "[INFO] DirectoryScanThread: Starting scan of " << directory_path_ << std::endl;

    if (progress_reporter_) {
	progress_reporter_->report_progress(0, 0, "Starting directory scan...");
    }

    Timer timer;
    StatusReporter reporter(10); // Report every 10 seconds
    reporter.start();

    try {
	// Use the existing parallel scanning functionality
	int result = database_->scan_directory_parallel(directory_path_, timer, reporter);

	if (progress_reporter_) {
	    if (result >= 0) {
		progress_reporter_->report_progress(result, result, "Scan completed successfully");
	    } else {
		progress_reporter_->report_progress(0, 0, "Scan cancelled or failed");
	    }
	}

	std::cout << "[INFO] DirectoryScanThread: Scan completed with result " << result << std::endl;

    } catch (const std::exception& e) {
	std::cerr << "[ERROR] DirectoryScanThread: Exception during scan: " << e.what() << std::endl;
	if (progress_reporter_) {
	    progress_reporter_->report_progress(0, 0, "Scan failed with exception");
	}
    }

    reporter.stop();
    GlobalFlags::set_scanning(false);

    std::cout << "[INFO] DirectoryScanThread: Thread exiting" << std::endl;
}

void DirectoryScanThread::scan_directory_recursive(const std::string& directory) {
    // This would be used for manual directory traversal if needed
    // For now, we use the existing DatabaseManager::scan_directory_parallel
}

//==============================================================================
// UpdateMonitorThread Implementation
//==============================================================================

UpdateMonitorThread::UpdateMonitorThread() : should_stop_(false) {
}

UpdateMonitorThread::~UpdateMonitorThread() {
    stop_monitoring();
    join();
}

void UpdateMonitorThread::start_monitoring(std::shared_ptr<DirectoryScanThread> scan_thread) {
    if (monitor_thread_.joinable()) {
	return; // Already monitoring
    }

    scan_thread_ = scan_thread;
    should_stop_.store(false);
    monitor_thread_ = std::thread(&UpdateMonitorThread::monitor_thread_main, this);
}

void UpdateMonitorThread::stop_monitoring() {
    should_stop_.store(true);
}

void UpdateMonitorThread::join() {
    if (monitor_thread_.joinable()) {
	monitor_thread_.join();
    }
}

void UpdateMonitorThread::monitor_thread_main() {
    std::cout << "[DEBUG] UpdateMonitorThread: Starting monitoring thread" << std::endl;
    std::cout.flush();

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	// Monitor scan progress and handle UI updates
	if (GlobalFlags::is_scanning()) {
	    // Use Fl::awake to notify UI of incremental updates
	    // This is called from a worker thread, so we need Fl::awake
	    static auto ui_update_callback = [](void* data) {
		// UI update callback - this runs in the main UI thread
		// Update progress bars, refresh layouts, etc.
	    };

	    std::cout << "[DEBUG] UpdateMonitorThread: Notifying UI via Fl::awake() for scan progress update" << std::endl;
	    std::cout.flush();
	    Fl::awake(ui_update_callback, nullptr);
	}

	// Check for cancellation requests
	if (GlobalFlags::is_cancel_requested()) {
	    std::cout << "[DEBUG] UpdateMonitorThread: Cancel request detected, stopping scan thread" << std::endl;
	    if (scan_thread_) {
		scan_thread_->stop_scan();
	    }
	    GlobalFlags::should_cancel_scan.store(false); // Reset flag
	}

	// Sleep to avoid busy waiting
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[DEBUG] UpdateMonitorThread: Thread exiting" << std::endl;
    std::cout.flush();
}

//==============================================================================
// WorkerPool Implementation
//==============================================================================

WorkerPool::WorkerPool() : should_stop_(false), processed_count_(0), active_workers_(0) {
}

WorkerPool::~WorkerPool() {
    stop_workers();
    join_all();
}

void WorkerPool::start_workers(int num_workers) {
    if (!worker_threads_.empty()) {
	return; // Already started
    }

    if (num_workers <= 0) {
	num_workers = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    }

    should_stop_.store(false);
    worker_threads_.reserve(num_workers);

    for (int i = 0; i < num_workers; ++i) {
	worker_threads_.emplace_back(&WorkerPool::worker_thread_main, this);
    }

    std::cout << "[INFO] WorkerPool: Started " << num_workers << " worker threads" << std::endl;
}

void WorkerPool::stop_workers() {
    should_stop_.store(true);
}

void WorkerPool::join_all() {
    for (auto& thread : worker_threads_) {
	if (thread.joinable()) {
	    thread.join();
	}
    }
    worker_threads_.clear();
}

void WorkerPool::worker_thread_main() {
    active_workers_.fetch_add(1);

    std::cout << "[DEBUG] WorkerPool: Worker thread started" << std::endl;
    std::cout.flush();

    ThumbnailGenerationTask task{"", "", ImageInfo{}};

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	bool found_task = false;

	// Try to get work from scan thread
	if (scan_thread_ && scan_thread_->thumbnail_gen_queue_.try_dequeue(task)) {
	    std::cout << "[DEBUG] WorkerPool: Dequeued thumbnail generation task from rectLayoutQueue - file: " << task.file_path << ", hash: " << task.hash << std::endl;
	    found_task = true;
	    processed_count_.fetch_add(1);

	    // Process the thumbnail generation task from directory scan
	    // This connects to the existing DatabaseManager thumbnail generation pipeline
	    if (scan_thread_ && scan_thread_->database_) {
		std::cout << "[DEBUG] WorkerPool: Generating thumbnails for file: " << task.file_path << std::endl;
		// Use the existing optimized thumbnail generation method that handles:
		// - JPEG DCT-domain downscaling for efficiency
		// - Multiple thumbnail sizes (32, 64, 128, 256, 512, 1024px)
		// - EXIF orientation correction
		// - Error handling for corrupt images
		bool success = scan_thread_->database_->generate_thumbnails_for_hash(task.hash, task.file_path);
		
		if (success) {
		    std::cout << "[DEBUG] WorkerPool: Successfully generated thumbnails, enqueuing write task to writeQueue - hash: " << task.hash << std::endl;
		    // Create a completion task for the writer thread
		    // Note: generate_thumbnails_for_hash handles its own database writes,
		    // so this is mainly for progress tracking
		    WriteTask write_task(WriteTask::STORE_THUMBNAIL, task.hash, std::vector<uint8_t>());
		    write_queue_.enqueue(std::move(write_task));
		} else {
		    std::cout << "[DEBUG] WorkerPool: Failed to generate thumbnails for file: " << task.file_path << " (hash: " << task.hash << ")" << std::endl;
		    std::cerr << "[WARNING] WorkerPool: Failed to generate thumbnails for " 
			      << task.file_path << " (hash: " << task.hash << ")" << std::endl;
		}
	    }
	}

	if (!found_task) {
	    // No work available, sleep briefly
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
    }

    active_workers_.fetch_sub(1);
    std::cout << "[DEBUG] WorkerPool: Worker thread exiting" << std::endl;
    std::cout.flush();
}

//==============================================================================
// WriterThread Implementation
//==============================================================================

WriterThread::WriterThread() : should_stop_(false), write_count_(0) {
}

WriterThread::~WriterThread() {
    stop_writing();
    join();
}

void WriterThread::start_writing(std::shared_ptr<WorkerPool> worker_pool) {
    if (writer_thread_.joinable()) {
	return; // Already started
    }

    worker_pool_ = worker_pool;
    should_stop_.store(false);
    writer_thread_ = std::thread(&WriterThread::writer_thread_main, this);
}

void WriterThread::stop_writing() {
    should_stop_.store(true);
}

void WriterThread::join() {
    if (writer_thread_.joinable()) {
	writer_thread_.join();
    }
}

void WriterThread::writer_thread_main() {
    std::cout << "[DEBUG] WriterThread: Starting writer thread" << std::endl;
    std::cout.flush();

    WriteTask task;
    std::vector<WriteTask> batch;
    const size_t BATCH_SIZE = 100;

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	bool found_task = false;

	// Try to get work from worker pool
	if (worker_pool_ && worker_pool_->get_write_queue().try_dequeue(task)) {
	    std::cout << "[DEBUG] WriterThread: Dequeued write task from writeQueue - type: " << task.type << ", key: " << task.key << std::endl;
	    found_task = true;
	    batch.push_back(std::move(task));
	}

	// Process batch if it's full or if we're stopping
	if (batch.size() >= BATCH_SIZE || (should_stop_.load() && !batch.empty())) {
	    std::cout << "[DEBUG] WriterThread: Processing batch of " << batch.size() << " write tasks" << std::endl;
	    // Process the batch of write tasks
	    write_count_.fetch_add(batch.size());

	    // Batch database operations would go here
	    // ... LMDB transaction logic ...

	    batch.clear();
	}

	if (!found_task) {
	    // No work available, sleep briefly
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
    }

    // Process any remaining batch items
    if (!batch.empty()) {
	std::cout << "[DEBUG] WriterThread: Processing final batch of " << batch.size() << " write tasks before exit" << std::endl;
	write_count_.fetch_add(batch.size());
	// ... process final batch ...
    }

    std::cout << "[DEBUG] WriterThread: Thread exiting, processed " << write_count_.load() << " writes" << std::endl;
    std::cout.flush();
}

//==============================================================================
// ThumbnailWorkers Implementation
//==============================================================================

ThumbnailWorkers::ThumbnailWorkers() : should_stop_(false), active_tasks_(0), completed_tasks_(0) {
}

ThumbnailWorkers::~ThumbnailWorkers() {
    stop_workers();
    join_all();
}

void ThumbnailWorkers::start_workers(int num_workers) {
    if (!worker_threads_.empty()) {
	return; // Already started
    }

    if (num_workers <= 0) {
	// Use fewer threads for UI thumbnails (they're lighter weight)
	num_workers = std::max(1, std::min(4, static_cast<int>(std::thread::hardware_concurrency() / 2)));
    }

    should_stop_.store(false);
    worker_threads_.reserve(num_workers);

    for (int i = 0; i < num_workers; ++i) {
	worker_threads_.emplace_back(&ThumbnailWorkers::thumbnail_worker_thread_main, this);
    }

    std::cout << "[INFO] ThumbnailWorkers: Started " << num_workers << " thumbnail worker threads" << std::endl;
}

void ThumbnailWorkers::stop_workers() {
    should_stop_.store(true);
}

void ThumbnailWorkers::join_all() {
    for (auto& thread : worker_threads_) {
	if (thread.joinable()) {
	    thread.join();
	}
    }
    worker_threads_.clear();
}

void ThumbnailWorkers::enqueue_high_priority(const UIThumbnailTask& task) {
    std::cout << "[DEBUG] ThumbnailWorkers: Enqueuing high priority task to highPriorityQueue - image_index: " << task.image_index << ", hash: " << task.hash << ", size: " << task.target_width << "x" << task.target_height << std::endl;
    std::cout.flush();
    high_priority_queue_.enqueue(task);
}

void ThumbnailWorkers::enqueue_low_priority(const UIThumbnailTask& task) {
    std::cout << "[DEBUG] ThumbnailWorkers: Enqueuing low priority task to lowPriorityQueue - image_index: " << task.image_index << ", hash: " << task.hash << ", size: " << task.target_width << "x" << task.target_height << std::endl;
    std::cout.flush();
    low_priority_queue_.enqueue(task);
}

bool ThumbnailWorkers::try_dequeue_result(UIDrawTask& result) {
    bool success = result_queue_.try_dequeue(result);
    if (success) {
	std::cout << "[DEBUG] ThumbnailWorkers: Dequeued draw result from result_queue - image_index: " << result.image_index << ", cache_key: " << result.cache_key << std::endl;
	std::cout.flush();
    }
    return success;
}

void ThumbnailWorkers::thumbnail_worker_thread_main() {
    std::cout << "[DEBUG] ThumbnailWorkers: Thumbnail worker thread started" << std::endl;
    std::cout.flush();

    UIThumbnailTask task;
    int tasks_processed = 0;

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	bool found_task = false;

	// Try high priority queue first
	if (high_priority_queue_.try_dequeue(task)) {
	    std::cout << "[DEBUG] ThumbnailWorkers: Dequeued task from highPriorityQueue - image_index: " << task.image_index << ", hash: " << task.hash << std::endl;
	    found_task = true;
	} else if (low_priority_queue_.try_dequeue(task)) {
	    std::cout << "[DEBUG] ThumbnailWorkers: Dequeued task from lowPriorityQueue - image_index: " << task.image_index << ", hash: " << task.hash << std::endl;
	    found_task = true;
	}

	if (found_task) {
	    active_tasks_.fetch_add(1);
	    tasks_processed++;
	    std::cout << "[DEBUG] ThumbnailWorkers: Processing thumbnail task - image_index: " << task.image_index << ", target size: " << task.target_width << "x" << task.target_height << std::endl;

	    // Generate UI thumbnail
	    std::unique_ptr<Fl_RGB_Image> thumbnail;
	    std::string cache_key = task.hash + "_" +
		std::to_string(task.target_width) + "x" +
		std::to_string(task.target_height);

	    // Generate UI thumbnail from database
	    thumbnail = generate_ui_thumbnail(task);

	    // Queue result for UI
	    if (thumbnail) {
		std::cout << "[DEBUG] ThumbnailWorkers: Generated thumbnail successfully, enqueuing to result_queue - image_index: " << task.image_index << ", cache_key: " << cache_key << std::endl;
		UIDrawTask result(task.image_index, std::move(thumbnail), cache_key);
		result_queue_.enqueue(std::move(result));

		// Notify UI that thumbnail is ready
		static auto thumbnail_ready_callback = [](void* data) {
		    // This runs in the main UI thread
		    // Trigger layout refresh, redraw, etc.
		};

		std::cout << "[DEBUG] ThumbnailWorkers: Notifying UI via Fl::awake() for completed thumbnail - image_index: " << task.image_index << std::endl;
		std::cout.flush();
		Fl::awake(thumbnail_ready_callback, nullptr);
	    } else {
		std::cout << "[DEBUG] ThumbnailWorkers: Failed to generate thumbnail for task - image_index: " << task.image_index << ", hash: " << task.hash << std::endl;
	    }

	    active_tasks_.fetch_sub(1);
	    completed_tasks_.fetch_add(1);
	} else {
	    // No work available, sleep briefly
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
    }

    std::cout << "[DEBUG] ThumbnailWorkers: Thumbnail worker thread exiting, processed "
	      << tasks_processed << " tasks" << std::endl;
    std::cout.flush();
}

std::unique_ptr<Fl_RGB_Image> ThumbnailWorkers::generate_ui_thumbnail(const UIThumbnailTask& task) {
    // Generate a UI-ready thumbnail from database-stored thumbnail data
    // This function handles the complete pipeline from LMDB lookup to FLTK image creation
    
    std::cout << "[DEBUG] ThumbnailWorkers: Starting thumbnail generation from LMDB - hash: " << task.hash << ", target size: " << task.target_width << "x" << task.target_height << std::endl;
    
    if (!database_) {
	std::cout << "[DEBUG] ThumbnailWorkers: Database not available, creating placeholder - hash: " << task.hash << std::endl;
	std::cerr << "[WARNING] ThumbnailWorkers: No database available for hash " << task.hash << std::endl;
	return create_placeholder_thumbnail(task.target_width, task.target_height, "No DB");
    }

    // Find the best thumbnail size from the database
    // We have pre-generated thumbnails at: 32, 64, 128, 256, 512, 1024 pixels
    std::vector<int> available_sizes = {32, 64, 128, 256, 512, 1024};
    int best_size = 0;
    
    // Choose the smallest thumbnail that's >= our target size for best quality
    for (int size : available_sizes) {
	if (size >= std::max(task.target_width, task.target_height)) {
	    best_size = size;
	    break;
	}
    }
    
    // If no size is large enough, use the largest available (1024px)
    if (best_size == 0) {
	best_size = 1024;
    }
    
    std::cout << "[DEBUG] ThumbnailWorkers: Selected best thumbnail size " << best_size << "px for hash: " << task.hash << std::endl;

    // Try to load thumbnail from database using LMDB transaction
    MDB_txn* read_txn = nullptr;
    if (!database_->begin_read_transaction(read_txn)) {
	std::cout << "[DEBUG] ThumbnailWorkers: Failed to begin LMDB transaction, creating error placeholder - hash: " << task.hash << std::endl;
	std::cerr << "[ERROR] ThumbnailWorkers: Failed to begin read transaction for hash " << task.hash << std::endl;
	return create_placeholder_thumbnail(task.target_width, task.target_height, "DB Error");
    }

    // Construct database key: "hash:thumb_size" format
    std::string thumb_key = task.hash + ":thumb_" + std::to_string(best_size);
    std::vector<uint8_t> thumb_data;
    
    std::cout << "[DEBUG] ThumbnailWorkers: Looking up thumbnail in LMDB - key: " << thumb_key << std::endl;
    bool success = database_->get_key_data(read_txn, thumb_key, thumb_data);
    
    database_->commit_transaction(read_txn);

    if (!success || thumb_data.empty()) {
	std::cout << "[DEBUG] ThumbnailWorkers: Primary thumbnail size not found, trying fallback sizes - hash: " << task.hash << std::endl;
	// Try fallback sizes if the preferred size isn't available
	// This handles cases where thumbnail generation was incomplete
	for (int fallback_size : available_sizes) {
	    if (fallback_size == best_size) continue; // Already tried this one
	    
	    std::cout << "[DEBUG] ThumbnailWorkers: Trying fallback size " << fallback_size << "px - hash: " << task.hash << std::endl;
	    if (!database_->begin_read_transaction(read_txn)) {
		break;
	    }
	    
	    std::string fallback_key = task.hash + ":thumb_" + std::to_string(fallback_size);
	    success = database_->get_key_data(read_txn, fallback_key, thumb_data);
	    database_->commit_transaction(read_txn);
	    
	    if (success && !thumb_data.empty()) {
		std::cout << "[DEBUG] ThumbnailWorkers: Found thumbnail at fallback size " << fallback_size << "px - hash: " << task.hash << std::endl;
		best_size = fallback_size; // Update for correct decoding
		break;
	    }
	}
	
	if (!success || thumb_data.empty()) {
	    std::cout << "[DEBUG] ThumbnailWorkers: No thumbnail found in LMDB, creating 'Not Found' placeholder - hash: " << task.hash << std::endl;
	    std::cerr << "[WARNING] ThumbnailWorkers: No thumbnail found for hash " << task.hash << std::endl;
	    return create_placeholder_thumbnail(task.target_width, task.target_height, "Not Found");
	}
    }
    
    std::cout << "[DEBUG] ThumbnailWorkers: Found thumbnail data in LMDB - hash: " << task.hash << ", size: " << thumb_data.size() << " bytes" << std::endl;

    // Decode JPEG thumbnail data using stb_image
    // All thumbnails are stored as JPEG with 90% quality for optimal size/quality balance
    int thumb_width, thumb_height, thumb_channels;
    std::cout << "[DEBUG] ThumbnailWorkers: Decoding JPEG thumbnail data - hash: " << task.hash << std::endl;
    unsigned char* rgb_data = stbi_load_from_memory(
	thumb_data.data(), thumb_data.size(),
	&thumb_width, &thumb_height, &thumb_channels, 3  // Force RGB output
    );

    if (!rgb_data) {
	std::cout << "[DEBUG] ThumbnailWorkers: Failed to decode JPEG, creating decode error placeholder - hash: " << task.hash << ", reason: " << stbi_failure_reason() << std::endl;
	std::cerr << "[ERROR] ThumbnailWorkers: Failed to decode thumbnail for hash " << task.hash 
		  << ": " << stbi_failure_reason() << std::endl;
	return create_placeholder_thumbnail(task.target_width, task.target_height, "Decode Error");
    }

    std::cout << "[DEBUG] ThumbnailWorkers: Successfully decoded thumbnail - hash: " << task.hash << ", decoded size: " << thumb_width << "x" << thumb_height << std::endl;

    // Resize thumbnail to match UI requirements while preserving aspect ratio
    int final_width = thumb_width;
    int final_height = thumb_height;
    
    unsigned char* final_data = rgb_data;
    if (thumb_width != task.target_width || thumb_height != task.target_height) {
	std::cout << "[DEBUG] ThumbnailWorkers: Resizing thumbnail to target size - hash: " << task.hash << ", from: " << thumb_width << "x" << thumb_height << " to target: " << task.target_width << "x" << task.target_height << std::endl;
	// Calculate aspect-preserving dimensions
	double aspect = (double)thumb_width / thumb_height;
	double target_aspect = (double)task.target_width / task.target_height;
	
	if (aspect > target_aspect) {
	    // Image is wider than target - fit to width
	    final_width = task.target_width;
	    final_height = (int)(task.target_width / aspect);
	} else {
	    // Image is taller than target - fit to height
	    final_height = task.target_height;
	    final_width = (int)(task.target_height * aspect);
	}
	
	// Allocate memory for resized image
	final_data = (unsigned char*)malloc(final_width * final_height * 3);
	if (!final_data) {
	    stbi_image_free(rgb_data);
	    std::cerr << "[ERROR] ThumbnailWorkers: Failed to allocate memory for resizing hash " << task.hash << std::endl;
	    return create_placeholder_thumbnail(task.target_width, task.target_height, "Memory Error");
	}
	
	// Resize using stb_image_resize with linear interpolation for good quality
	if (!stbir_resize_uint8_linear(
	    rgb_data, thumb_width, thumb_height, 0,
	    final_data, final_width, final_height, 0,
	    STBIR_RGB)) {
	    free(final_data);
	    stbi_image_free(rgb_data);
	    std::cerr << "[ERROR] ThumbnailWorkers: Failed to resize thumbnail for hash " << task.hash << std::endl;
	    return create_placeholder_thumbnail(task.target_width, task.target_height, "Resize Error");
	}
	
	stbi_image_free(rgb_data);
    }

    // Create FLTK RGB image - Fl_RGB_Image takes ownership of the data pointer
    auto thumbnail = std::make_unique<Fl_RGB_Image>(final_data, final_width, final_height, 3);
    
    return thumbnail;
}

std::unique_ptr<Fl_RGB_Image> ThumbnailWorkers::create_placeholder_thumbnail(int width, int height, const std::string& message) {
    // Create a visual placeholder for failed/missing thumbnails
    // Different error types get different visual indicators:
    // - "Error"/"Decode Error": Red tinted center (image decode failure)
    // - "Not Found": Blue tinted center (missing thumbnail data)
    // - Others: Grey with dark border (general errors)
    
    std::cout << "[DEBUG] ThumbnailWorkers: Creating placeholder thumbnail - size: " << width << "x" << height << ", message: " << message << std::endl;
    std::cout.flush();
    
    int data_size = width * height * 3;
    unsigned char* placeholder_data = (unsigned char*)malloc(data_size);
    
    if (!placeholder_data) {
	std::cout << "[DEBUG] ThumbnailWorkers: Failed to allocate memory for placeholder" << std::endl;
	return nullptr;
    }
    
    // Fill with medium grey background (RGB: 128, 128, 128)
    for (int i = 0; i < data_size; i += 3) {
	placeholder_data[i] = 128;     // R
	placeholder_data[i + 1] = 128; // G 
	placeholder_data[i + 2] = 128; // B
    }
    
    // Add a darker border (2 pixels wide) for visual definition
    for (int y = 0; y < height; y++) {
	for (int x = 0; x < width; x++) {
	    if (x < 2 || x >= width - 2 || y < 2 || y >= height - 2) {
		int offset = (y * width + x) * 3;
		placeholder_data[offset] = 64;     // R
		placeholder_data[offset + 1] = 64; // G
		placeholder_data[offset + 2] = 64; // B
	    }
	}
    }
    
    // Add visual indication based on error type in center third of image
    if (message == "Error" || message == "Decode Error") {
	// Add red tint for decode errors - indicates corrupt/unreadable image data
	for (int y = height/3; y < 2*height/3; y++) {
	    for (int x = width/3; x < 2*width/3; x++) {
		int offset = (y * width + x) * 3;
		placeholder_data[offset] = std::min(255, (int)placeholder_data[offset] + 50);     // R+
		placeholder_data[offset + 1] = std::max(0, (int)placeholder_data[offset + 1] - 20); // G-
		placeholder_data[offset + 2] = std::max(0, (int)placeholder_data[offset + 2] - 20); // B-
	    }
	}
    } else if (message == "Not Found") {
	// Add blue tint for missing thumbnails - indicates missing database entry
	for (int y = height/3; y < 2*height/3; y++) {
	    for (int x = width/3; x < 2*width/3; x++) {
		int offset = (y * width + x) * 3;
		placeholder_data[offset] = std::max(0, (int)placeholder_data[offset] - 20);     // R-
		placeholder_data[offset + 1] = std::max(0, (int)placeholder_data[offset + 1] - 20); // G-
		placeholder_data[offset + 2] = std::min(255, (int)placeholder_data[offset + 2] + 50); // B+
	    }
	}
    }
    
    return std::make_unique<Fl_RGB_Image>(placeholder_data, width, height, 3);
}

//==============================================================================
// ThreadManager Implementation
//==============================================================================

ThreadManager::ThreadManager() : initialized_(false) {
    initialize_threads();
}

ThreadManager::~ThreadManager() {
    shutdown_all();
    cleanup_threads();
}

void ThreadManager::initialize_threads() {
    if (initialized_) {
	return;
    }

    // Create shared components
    progress_reporter_ = std::make_shared<ProgressReporter>();

    // Create thread components
    scan_thread_ = std::make_shared<DirectoryScanThread>();
    monitor_thread_ = std::make_shared<UpdateMonitorThread>();
    worker_pool_ = std::make_shared<WorkerPool>();
    writer_thread_ = std::make_shared<WriterThread>();
    thumbnail_workers_ = std::make_shared<ThumbnailWorkers>();

    // Set up connections
    scan_thread_->set_progress_reporter(progress_reporter_);
    monitor_thread_->set_progress_reporter(progress_reporter_);
    worker_pool_->set_scan_thread(scan_thread_);

    initialized_ = true;

    std::cout << "[INFO] ThreadManager: Thread system initialized" << std::endl;
}

void ThreadManager::cleanup_threads() {
    if (!initialized_) {
	return;
    }

    // Clean up in reverse order
    thumbnail_workers_.reset();
    writer_thread_.reset();
    worker_pool_.reset();
    monitor_thread_.reset();
    scan_thread_.reset();
    progress_reporter_.reset();

    initialized_ = false;

    std::cout << "[INFO] ThreadManager: Thread system cleaned up" << std::endl;
}

bool ThreadManager::start_directory_scan(const std::string& directory_path, const std::string& db_path) {
    if (!initialized_) {
	return false;
    }

    // Start all supporting threads first
    worker_pool_->start_workers();
    writer_thread_->start_writing(worker_pool_);
    thumbnail_workers_->start_workers();
    monitor_thread_->start_monitoring(scan_thread_);

    // Start the scan
    bool result = scan_thread_->start_scan(directory_path, db_path);

    if (result) {
	std::cout << "[INFO] ThreadManager: Started directory scan for " << directory_path << std::endl;
    } else {
	std::cerr << "[ERROR] ThreadManager: Failed to start directory scan" << std::endl;
    }

    return result;
}

void ThreadManager::cancel_scan() {
    GlobalFlags::request_cancel();
    std::cout << "[INFO] ThreadManager: Scan cancellation requested" << std::endl;
}

void ThreadManager::shutdown_all() {
    GlobalFlags::request_shutdown();

    if (initialized_) {
	// Stop all threads in order
	if (scan_thread_) scan_thread_->stop_scan();
	if (monitor_thread_) monitor_thread_->stop_monitoring();
	if (worker_pool_) worker_pool_->stop_workers();
	if (writer_thread_) writer_thread_->stop_writing();
	if (thumbnail_workers_) thumbnail_workers_->stop_workers();

	// Wait for threads to complete
	if (scan_thread_) scan_thread_->join();
	if (monitor_thread_) monitor_thread_->join();
	if (worker_pool_) worker_pool_->join_all();
	if (writer_thread_) writer_thread_->join();
	if (thumbnail_workers_) thumbnail_workers_->join_all();
    }

    std::cout << "[INFO] ThreadManager: All threads shut down" << std::endl;
}

void ThreadManager::set_progress_callback(ProgressReporter::ProgressCallback callback) {
    if (progress_reporter_) {
	progress_reporter_->set_progress_callback(callback);
    }
}

void ThreadManager::set_metadata_callback(DirectoryScanThread::ImageMetadataCallback callback) {
    if (scan_thread_) {
	scan_thread_->set_metadata_callback(callback);
    }
}

void ThreadManager::request_thumbnail(const UIThumbnailTask& task) {
    if (thumbnail_workers_) {
	if (task.priority == UIThumbnailTask::HIGH) {
	    thumbnail_workers_->enqueue_high_priority(task);
	} else {
	    thumbnail_workers_->enqueue_low_priority(task);
	}
    }
}

bool ThreadManager::get_thumbnail_result(UIDrawTask& result) {
    if (thumbnail_workers_) {
	return thumbnail_workers_->try_dequeue_result(result);
    }
    return false;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s