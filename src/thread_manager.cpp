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
#include "logging.hpp"
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
	LOG_SCAN_BASIC("DirectoryScanThread: Failed to open database: " + db_path_);
	return false;
    }

    // Set up database callback for stage 1 metadata
    database_->set_image_info_callback([this](const ImageInfo& info) {
	LOG_SCAN_VERBOSE_IMG("DirectoryScanThread: Received image metadata callback - path: " + info.path + 
	                ", hash: " + info.hash + ", aspect_ratio: " + std::to_string(info.aspect_ratio), info.path);
	if (metadata_callback_) {
	    LOG_SCAN_VERBOSE_IMG("DirectoryScanThread: Invoking metadata callback for UI notification - path: " + info.path, info.path);
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
    LOG_SCAN_BASIC("DirectoryScanThread: Starting scan of " + directory_path_);

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

	LOG_SCAN_BASIC("DirectoryScanThread: Scan completed with result " + std::to_string(result));

    } catch (const std::exception& e) {
	LOG_SCAN_BASIC("DirectoryScanThread: Exception during scan: " + std::string(e.what()));
	if (progress_reporter_) {
	    progress_reporter_->report_progress(0, 0, "Scan failed with exception");
	}
    }

    reporter.stop();
    GlobalFlags::set_scanning(false);

    LOG_SCAN_BASIC("DirectoryScanThread: Thread exiting");
}

void DirectoryScanThread::scan_directory_recursive(const std::string& directory) {
    // This would be used for manual directory traversal if needed
    // For now, we use the existing DatabaseManager::scan_directory_parallel
}

//==============================================================================
// UpdateMonitorThread Implementation
//==============================================================================

UpdateMonitorThread::UpdateMonitorThread() : should_stop_(false), ui_notify_widget_(nullptr) {
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
    LOG_SCAN_BASIC("UpdateMonitorThread: Starting monitoring thread");

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	// Monitor scan progress and handle UI updates
	if (GlobalFlags::is_scanning()) {
	    // Use Fl::awake to notify UI of incremental updates
	    // This is called from a worker thread, so we need Fl::awake
	    auto ui_update_callback = [](void* data) {
		// UI update callback - this runs in the main UI thread
		UpdateMonitorThread* self = static_cast<UpdateMonitorThread*>(data);
		if (self && self->ui_notify_widget_) {
		    self->ui_notify_widget_->redraw();
		}
	    };

	    LOG_SCAN_BASIC("UpdateMonitorThread: Notifying UI via Fl::awake() for scan progress update");
	    Fl::awake(ui_update_callback, this);
	}

	// Check for cancellation requests
	if (GlobalFlags::is_cancel_requested()) {
	    LOG_SCAN_BASIC("UpdateMonitorThread: Cancel request detected, stopping scan thread");
	    if (scan_thread_) {
		scan_thread_->stop_scan();
	    }
	    GlobalFlags::should_cancel_scan.store(false); // Reset flag
	}

	// Sleep to avoid busy waiting
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    LOG_SCAN_BASIC("UpdateMonitorThread: Thread exiting");
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

    LOG_THREAD_BASIC("WorkerPool: Started " + std::to_string(num_workers) + " worker threads");
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

    LOG_THREAD_BASIC("WorkerPool: Worker thread started");

    ThumbnailGenerationTask task;

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	bool found_task = false;

	// Try to get work from scan thread with short timeout to remain responsive
	if (scan_thread_) {
	    // Use wait_dequeue_timed to block efficiently instead of polling
	    if (scan_thread_->thumbnail_gen_queue_.wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
		// Check for shutdown sentinel
		if (task.is_shutdown_sentinel) {
		    LOG_THREAD_BASIC("WorkerPool: Received shutdown sentinel, exiting worker thread");
		    break;
		}

		// Use image-aware logging for thumbnail generation tasks since file_path is available
		LOG_THREAD_VERBOSE_IMG("WorkerPool: Dequeued thumbnail generation task from thumbnail_gen_queue - file: " + 
		                   task.file_path + ", hash: " + task.hash, task.file_path);
		found_task = true;
		processed_count_.fetch_add(1);

		// Process the thumbnail generation task from directory scan
		// This connects to the existing DatabaseManager thumbnail generation pipeline
		if (scan_thread_->database_) {
		    LOG_THREAD_BASIC_IMG("WorkerPool: Generating thumbnails for file: " + task.file_path, task.file_path);
		    // Use the existing optimized thumbnail generation method that handles:
		    // - JPEG DCT-domain downscaling for efficiency
		    // - Multiple thumbnail sizes (32, 64, 128, 256, 512, 1024px)
		    // - EXIF orientation correction
		    // - Error handling for corrupt images
		    bool success = scan_thread_->database_->generate_thumbnails_for_hash(task.hash, task.file_path);

		    if (success) {
			// Use image-aware logging for thumbnail generation tasks since file_path is available
			LOG_THREAD_BASIC_IMG("WorkerPool: Successfully generated thumbnails, enqueuing write task to writeQueue - hash: " + task.hash, task.file_path);
			// Create a completion task for the writer thread
			// Note: generate_thumbnails_for_hash handles its own database writes,
			// so this is mainly for progress tracking
			WriteTask write_task(WriteTask::STORE_THUMBNAIL, task.hash, std::vector<uint8_t>());
			write_queue_.enqueue(std::move(write_task));
		    } else {
			// Use image-aware logging for thumbnail generation tasks since file_path is available
			LOG_THREAD_BASIC_IMG("WorkerPool: Failed to generate thumbnails for file: " + task.file_path + " (hash: " + task.hash + ")", task.file_path);
			std::cerr << "[WARNING] WorkerPool: Failed to generate thumbnails for "
				  << task.file_path << " (hash: " << task.hash << ")" << std::endl;
		    }
		}
	    }
	    // If wait_dequeue_timed returned false, it means timeout - continue loop to check shutdown flags
	}

	// No explicit sleep needed - wait_dequeue_timed handles efficient blocking
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
	// Try to get work from worker pool with short timeout for responsiveness
	if (worker_pool_) {
	    if (worker_pool_->get_write_queue().wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
		// Check for shutdown sentinel
		if (task.type == WriteTask::SHUTDOWN) {
		    std::cout << "[DEBUG] WriterThread: Received shutdown sentinel, exiting writer thread" << std::endl;
		    break;
		}

		std::cout << "[DEBUG] WriterThread: Dequeued write task from writeQueue - type: " << task.type << ", key: " << task.key << std::endl;
		batch.push_back(std::move(task));
	    }
	    // If wait_dequeue_timed returned false, it means timeout - continue to check batch processing
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

	// No explicit sleep needed - wait_dequeue_timed handles efficient blocking
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

ThumbnailWorkers::ThumbnailWorkers() : should_stop_(false), active_tasks_(0), completed_tasks_(0), ui_notify_widget_(nullptr) {
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
    
    // Clear all in-flight requests during shutdown to prevent memory leaks
    {
	std::lock_guard<std::mutex> lock(in_flight_mutex_);
	if (!in_flight_requests_.empty()) {
	    std::cout << "[DEBUG] ThumbnailWorkers: Clearing " << in_flight_requests_.size() << " in-flight requests during shutdown" << std::endl;
	    in_flight_requests_.clear();
	}
    }
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
    std::string cache_key = make_thumbnail_key(task.hash, task.target_width, task.target_height);
    
    // Deduplication: Check if a request for this thumbnail is already in progress
    if (is_request_in_flight(cache_key)) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Skipping duplicate high priority thumbnail request - cache_key: " + cache_key + " already in flight", task.path);
	return;
    }

    // Mark request as in-flight before enqueuing to prevent race conditions
    mark_request_in_flight(cache_key);
    
    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Enqueuing high priority thumbnail task - image_index: " + std::to_string(task.image_index) + ", cache_key: " + cache_key, task.path);
    high_priority_queue_.enqueue(task);
}

void ThumbnailWorkers::enqueue_low_priority(const UIThumbnailTask& task) {
    std::string cache_key = make_thumbnail_key(task.hash, task.target_width, task.target_height);
    
    // Deduplication: Check if a request for this thumbnail is already in progress
    if (is_request_in_flight(cache_key)) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Skipping duplicate low priority thumbnail request - cache_key: " + cache_key + " already in flight", task.path);
	return;
    }

    // Mark request as in-flight before enqueuing to prevent race conditions
    mark_request_in_flight(cache_key);
    
    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Enqueuing low priority thumbnail task - image_index: " + std::to_string(task.image_index) + ", cache_key: " +  cache_key, task.path);
    low_priority_queue_.enqueue(task);
}

bool ThumbnailWorkers::try_dequeue_result(UIDrawTask& result) {
    bool success = result_queue_.try_dequeue(result);
    if (success) {
	// Check for shutdown sentinel and handle accordingly
	if (result.is_shutdown_sentinel) {
	    LOG_THUMBNAIL_VERBOSE("Received shutdown sentinel on result queue");
	    return false;  // Don't return shutdown sentinels to the UI
	}
	LOG_THUMBNAIL_VERBOSE("Dequeued thumbnail draw result - image_index: " + std::to_string(result.image_index) + ", cache_key: " + result.cache_key);
    }
    return success;
}

void ThumbnailWorkers::flush_high_priority_queue() {
    // Drain all items from the high priority queue
    UIThumbnailTask dummy_task;
    size_t flushed_count = 0;
    while (high_priority_queue_.try_dequeue(dummy_task)) {
	flushed_count++;
    }
    std::cout << "[DEBUG] ThumbnailWorkers: Flushed " << flushed_count << " items from high priority queue" << std::endl;
    std::cout.flush();
}

uint64_t ThumbnailWorkers::get_next_generation_id() {
    return current_generation_id_.fetch_add(1) + 1;
}

void ThumbnailWorkers::thumbnail_worker_thread_main() {
    std::cout << "[DEBUG] ThumbnailWorkers: Thumbnail worker thread started" << std::endl;
    std::cout.flush();

    UIThumbnailTask task;
    int tasks_processed = 0;

    while (!should_stop_.load() && !GlobalFlags::is_shutdown_requested()) {
	bool found_task = false;

	// Try high priority queue first with short timeout for responsiveness
	if (high_priority_queue_.wait_dequeue_timed(task, std::chrono::milliseconds(50))) {
	    // Check for shutdown sentinel
	    if (task.is_shutdown_sentinel) {
		std::cout << "[DEBUG] ThumbnailWorkers: Received shutdown sentinel on high priority queue, exiting worker thread" << std::endl;
		break;
	    }
	    
	    // Check generation ID for high priority tasks - discard stale requests
	    if (task.generation_id != 0 && task.generation_id < current_generation_id_.load()) {
		LOG_THUMBNAIL_VERBOSE("Discarding stale high priority task - image_index: " + std::to_string(task.image_index) + 
		                     ", task_gen: " + std::to_string(task.generation_id) + ", current_gen: " + std::to_string(current_generation_id_.load()));
		continue;  // Skip this task and get next one
	    }
	    
	    // Use image-aware logging for thumbnail generation tasks since file_path is available
	    LOG_THREAD_VERBOSE_IMG("WorkerPool: Dequeued task from highPriorityQueue - image_index: " + std::to_string(task.image_index) + ", hash: " + task.hash + ", gen_id: " + std::to_string(task.generation_id), task.path);
	    found_task = true;
	} else if (low_priority_queue_.wait_dequeue_timed(task, std::chrono::milliseconds(50))) {
	    // Check for shutdown sentinel
	    if (task.is_shutdown_sentinel) {
		LOG_THUMBNAIL_VERBOSE("Received shutdown sentinel on low priority queue, exiting worker thread");
		break;
	    }
	    // Use image-aware logging for thumbnail generation tasks since path is available
	    LOG_THREAD_VERBOSE_IMG("WorkerPool: Dequeued task from lowPriorityQueue - image_index: " + std::to_string(task.image_index) + ", hash: " + task.hash, task.path);
	    found_task = true;
	}
	// If both wait_dequeue_timed calls returned false, it means timeout - continue loop to check shutdown flags

	if (found_task) {
	    active_tasks_.fetch_add(1);
	    tasks_processed++;
	    LOG_THUMBNAIL_VERBOSE("Processing thumbnail task - image_index: " + std::to_string(task.image_index) + ", target size: " + std::to_string(task.target_width) + "x" + std::to_string(task.target_height));

	    // Generate UI thumbnail
	    std::unique_ptr<Fl_RGB_Image> thumbnail;
	    std::string cache_key = make_thumbnail_key(task.hash, task.target_width, task.target_height);

	    // Generate UI thumbnail from database
	    thumbnail = generate_ui_thumbnail(task);

	    // Mark the request as completed in deduplication tracker
	    // This allows future requests for the same hash:size to be processed
	    mark_request_completed(cache_key);

	    // Only queue real thumbnails for UI - placeholders are generated dynamically in draw code
	    if (thumbnail) {
		// Use image-aware logging when path is available for targeted filtering
		LOG_THUMBNAIL_BASIC_IMG("Generated real thumbnail successfully, enqueuing to result_queue - image_index: " + std::to_string(task.image_index) + ", cache_key: " + cache_key, task.path);
		UIDrawTask result(task.image_index, std::move(thumbnail), cache_key, task.path);
		result_queue_.enqueue(std::move(result));

		// Notify UI that thumbnail is ready
		auto thumbnail_ready_callback = [](void* data) {
		    // This runs in the main UI thread
		    ThumbnailWorkers* self = static_cast<ThumbnailWorkers*>(data);
		    if (self && self->ui_notify_widget_) {
			self->ui_notify_widget_->redraw();
		    }
		};

		// Use image-aware logging when path is available for targeted filtering
		LOG_THUMBNAIL_BASIC_IMG("Notifying UI via Fl::awake() for completed thumbnail - image_index: " + std::to_string(task.image_index), task.path);
		Fl::awake(thumbnail_ready_callback, this);
	    } else {
		// Use image-aware logging when path is available for targeted filtering
		LOG_THUMBNAIL_VERBOSE_IMG("No real thumbnail available for task - image_index: " + std::to_string(task.image_index) + ", hash: " + task.hash + " (placeholder will be generated in UI)", task.path);
	    }

	    active_tasks_.fetch_sub(1);
	    completed_tasks_.fetch_add(1);
	}

	// No explicit sleep needed - wait_dequeue_timed handles efficient blocking
    }

    LOG_THUMBNAIL_VERBOSE("Thumbnail worker thread exiting, processed " + std::to_string(tasks_processed) + " tasks");
}

std::unique_ptr<Fl_RGB_Image> ThumbnailWorkers::generate_ui_thumbnail(const UIThumbnailTask& task) {
    // Generate a UI-ready thumbnail from database-stored thumbnail data
    // This function handles the complete pipeline from LMDB lookup to FLTK image creation
    // Returns nullptr if no real thumbnail is available - placeholders are handled in UI

    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Starting thumbnail generation from LMDB - cache_key: " + make_thumbnail_key(task.hash, task.target_width, task.target_height), task.path);

    if (!database_) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Database not available, no thumbnail to generate - hash: " + task.hash, task.path);
	std::cerr << "[WARNING] ThumbnailWorkers: No database available for hash " << task.hash << std::endl;
	return nullptr;
    }

    // Find the best thumbnail size from the database using canonical size function
    // This ensures consistency with the UI cache lookup system
    int best_size = pick_thumbnail_size(task.target_width, task.target_height);

    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Selected best thumbnail size " + std::to_string(best_size) + "px for hash: " + task.hash, task.path);

    // Try to load thumbnail from database using LMDB transaction
    MDB_txn* read_txn = nullptr;
    if (!database_->begin_read_transaction(read_txn)) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Failed to begin LMDB transaction, no thumbnail available - hash: " + task.hash, task.path);
	std::cerr << "[ERROR] ThumbnailWorkers: Failed to begin read transaction for hash " << task.hash << std::endl;
	return nullptr;
    }

    // Construct database key: "hash:size" format (consistent format)
    std::string thumb_key = make_thumbnail_key(task.hash, best_size);
    std::vector<uint8_t> thumb_data;

    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Looking up thumbnail in LMDB - key: " + thumb_key, task.path);
    bool success = database_->get_key_data(read_txn, thumb_key, thumb_data);

    database_->commit_transaction(read_txn);

    if (!success || thumb_data.empty()) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Primary thumbnail size not found, trying fallback sizes - hash: " + task.hash, task.path);
	// Try fallback sizes if the preferred size isn't available
	// This handles cases where thumbnail generation was incomplete
	static const std::vector<int> available_sizes = {32, 64, 128, 256, 512, 1024};
	for (int fallback_size : available_sizes) {
	    if (fallback_size == best_size) continue; // Already tried this one

	    // Use image-aware logging when path is available for targeted filtering
	    LOG_THUMBNAIL_VERBOSE_IMG("Trying fallback size " + std::to_string(fallback_size) + "px - hash: " + task.hash, task.path);
	    if (!database_->begin_read_transaction(read_txn)) {
		break;
	    }

	    std::string fallback_key = make_thumbnail_key(task.hash, fallback_size);
	    success = database_->get_key_data(read_txn, fallback_key, thumb_data);
	    database_->commit_transaction(read_txn);

	    if (success && !thumb_data.empty()) {
		// Use image-aware logging when path is available for targeted filtering
		LOG_THUMBNAIL_VERBOSE_IMG("Found thumbnail at fallback size " + std::to_string(fallback_size) + "px - hash: " + task.hash, task.path);
		best_size = fallback_size; // Update for correct decoding
		break;
	    }
	}

	if (!success || thumb_data.empty()) {
	    // Use image-aware logging when path is available for targeted filtering
	    LOG_THUMBNAIL_VERBOSE_IMG("No thumbnail found in LMDB, no real thumbnail available - hash: " + task.hash, task.path);
	    std::cerr << "[WARNING] ThumbnailWorkers: No thumbnail found for hash " << task.hash << std::endl;
	    return nullptr;
	}
    }

    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Found thumbnail data in LMDB - hash: " + task.hash + ", size: " + std::to_string(thumb_data.size()) + " bytes", task.path);

    // Decode JPEG thumbnail data using stb_image
    // All thumbnails are stored as JPEG with 90% quality for optimal size/quality balance
    int thumb_width, thumb_height, thumb_channels;
    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Decoding JPEG thumbnail data - hash: " + task.hash, task.path);
    unsigned char* rgb_data = stbi_load_from_memory(
	thumb_data.data(), thumb_data.size(),
	&thumb_width, &thumb_height, &thumb_channels, 3  // Force RGB output
    );

    if (!rgb_data) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Failed to decode JPEG, no thumbnail available - hash: " + task.hash + ", reason: " + std::string(stbi_failure_reason()), task.path);
	std::cerr << "[ERROR] ThumbnailWorkers: Failed to decode thumbnail for hash " << task.hash
		  << ": " << stbi_failure_reason() << std::endl;
	return nullptr;
    }

    // Use image-aware logging when path is available for targeted filtering
    LOG_THUMBNAIL_VERBOSE_IMG("Successfully decoded thumbnail - hash: " + task.hash + ", decoded size: " + std::to_string(thumb_width) + "x" + std::to_string(thumb_height), task.path);

    // Resize thumbnail to match UI requirements while preserving aspect ratio
    int final_width = thumb_width;
    int final_height = thumb_height;

    unsigned char* final_data = rgb_data;
    if (thumb_width != task.target_width || thumb_height != task.target_height) {
	// Use image-aware logging when path is available for targeted filtering
	LOG_THUMBNAIL_VERBOSE_IMG("Resizing thumbnail to target size - hash: " + task.hash + ", from: " + std::to_string(thumb_width) + "x" + std::to_string(thumb_height) + " to target: " + std::to_string(task.target_width) + "x" + std::to_string(task.target_height), task.path);
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
	    return nullptr;
	}

	// Resize using stb_image_resize with linear interpolation for good quality
	if (!stbir_resize_uint8_linear(
	    rgb_data, thumb_width, thumb_height, 0,
	    final_data, final_width, final_height, 0,
	    STBIR_RGB)) {
	    free(final_data);
	    stbi_image_free(rgb_data);
	    std::cerr << "[ERROR] ThumbnailWorkers: Failed to resize thumbnail for hash " << task.hash << std::endl;
	    return nullptr;
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
// ThumbnailWorkers Deduplication Helper Methods
//==============================================================================

bool ThumbnailWorkers::is_request_in_flight(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    return in_flight_requests_.find(cache_key) != in_flight_requests_.end();
}

void ThumbnailWorkers::mark_request_in_flight(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    in_flight_requests_.insert(cache_key);
    std::cout << "[DEBUG] ThumbnailWorkers: Marked request in-flight - cache_key: " << cache_key << " (total in-flight: " << in_flight_requests_.size() << ")" << std::endl;
}

void ThumbnailWorkers::mark_request_completed(const std::string& cache_key) {
    std::lock_guard<std::mutex> lock(in_flight_mutex_);
    size_t removed = in_flight_requests_.erase(cache_key);
    if (removed > 0) {
	std::cout << "[DEBUG] ThumbnailWorkers: Marked request completed - cache_key: " << cache_key << " (total in-flight: " << in_flight_requests_.size() << ")" << std::endl;
    } else {
	std::cout << "[DEBUG] ThumbnailWorkers: Request already completed or not found - cache_key: " << cache_key << std::endl;
    }
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

    if (result && scan_thread_->get_database()) {
	thumbnail_workers_->set_database(scan_thread_->get_database());
    }

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
	// Enqueue shutdown sentinels to all blocking queues to wake up waiting threads
	// This ensures clean thread termination without indefinite blocking

	// Stop scan thread first to prevent new work from being generated
	if (scan_thread_) {
	    scan_thread_->stop_scan();

	    // Enqueue shutdown sentinels for thumbnail generation workers
	    if (worker_pool_) {
		// Send one shutdown sentinel per worker thread
		size_t num_workers = worker_pool_->get_worker_count();
		for (size_t i = 0; i < num_workers; ++i) {
		    scan_thread_->enqueue_shutdown_thumbnail_task();
		}
	    }
	}

	// Stop other threads and enqueue their shutdown sentinels
	if (monitor_thread_) monitor_thread_->stop_monitoring();
	if (worker_pool_) {
	    worker_pool_->stop_workers();

	    // Enqueue shutdown sentinels for writer thread
	    worker_pool_->get_write_queue().enqueue(WriteTask::create_shutdown_sentinel());
	}
	if (writer_thread_) writer_thread_->stop_writing();
	if (thumbnail_workers_) {
	    thumbnail_workers_->stop_workers();

	    // Enqueue shutdown sentinels for thumbnail workers (both priority queues)
	    size_t num_thumb_workers = thumbnail_workers_->get_worker_count();
	    for (size_t i = 0; i < num_thumb_workers; ++i) {
		thumbnail_workers_->enqueue_shutdown_tasks();
	    }

	    // Also send shutdown sentinel to result queue
	    thumbnail_workers_->enqueue_shutdown_result();
	}

	// Wait for threads to complete - they should exit cleanly after processing shutdown sentinels
	if (scan_thread_) scan_thread_->join();
	if (monitor_thread_) monitor_thread_->join();
	if (worker_pool_) worker_pool_->join_all();
	if (writer_thread_) writer_thread_->join();
	if (thumbnail_workers_) thumbnail_workers_->join_all();
    }

    std::cout << "[INFO] ThreadManager: All threads shut down cleanly" << std::endl;
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
    // Use image-aware logging when path is available for targeted filtering
    LOG_THREAD_BASIC_IMG("ThreadManager: Received thumbnail request - image_index: " + std::to_string(task.image_index) + 
                         ", priority: " + (task.priority == UIThumbnailTask::HIGH ? "HIGH" : "LOW") + ", hash: " + task.hash, task.path);
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
	bool success = thumbnail_workers_->try_dequeue_result(result);
	if (success) {
	    // Use image-aware logging when path is available for targeted filtering
	    LOG_THREAD_BASIC_IMG("ThreadManager: Retrieved thumbnail result - image_index: " + std::to_string(result.image_index) + 
	                         ", cache_key: " + result.cache_key, result.path);
	}
	return success;
    }
    return false;
}

void ThreadManager::flush_high_priority_queue() {
    if (thumbnail_workers_) {
	thumbnail_workers_->flush_high_priority_queue();
    }
}

uint64_t ThreadManager::get_next_generation_id() {
    if (thumbnail_workers_) {
	return thumbnail_workers_->get_next_generation_id();
    }
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
