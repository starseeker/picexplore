/*
 * thread_manager.hpp - New thread architecture for picexplore
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
#include <functional>
#include <filesystem>
#include <unordered_set>
#include <FL/Fl_RGB_Image.H>
#include "database.hpp"
#include "task_scheduler.hpp"
#include "concurrentqueue.h"
#include "blockingconcurrentqueue.h"

namespace fs = std::filesystem;

// Forward declarations
class DatabaseManager;
class Fl_Widget;

/**
 * Global shutdown and cancel mechanisms
 *
 * Shutdown Coordination:
 * - GlobalFlags::request_shutdown() signals all threads to begin shutdown
 * - BlockingConcurrentQueue with shutdown sentinels ensures clean thread termination
 * - Each queue type has shutdown sentinel support to wake blocked threads
 * - ThreadManager::shutdown_all() coordinates the complete shutdown sequence:
 *   1. Enqueue shutdown sentinels to all blocking queues
 *   2. Set shutdown flags
 *   3. Join all threads (which should exit after processing sentinels)
 */
class GlobalFlags {
public:
    static std::atomic<bool> should_shutdown;
    static std::atomic<bool> should_cancel_scan;
    static std::atomic<bool> scanning_active;

    static void request_shutdown() { should_shutdown.store(true); }
    static void request_cancel() { should_cancel_scan.store(true); }
    static bool is_shutdown_requested() { return should_shutdown.load(); }
    static bool is_cancel_requested() { return should_cancel_scan.load(); }
    static bool is_scanning() { return scanning_active.load(); }
    static void set_scanning(bool active) { scanning_active.store(active); }
};

/**
 * Task types for different queues
 *
 * Blocking Queue Pattern:
 * - All task types support shutdown sentinels via is_shutdown_sentinel flags
 * - Worker threads use wait_dequeue_timed() for responsive blocking
 * - Shutdown sentinels wake waiting threads for clean termination
 * - Sentinels are processed immediately and cause thread exit
 */
struct ScanTask {
    enum Type {
	SCAN_FILE,
	SCAN_COMPLETE,
	SHUTDOWN  // Sentinel task for clean thread shutdown
    };

    Type type;
    std::string file_path;

    ScanTask(Type t) : type(t) {}
    ScanTask(Type t, const std::string& path) : type(t), file_path(path) {}
};

struct ThumbnailGenerationTask {
    std::string file_path;
    std::string hash;
    ImageInfo metadata;
    bool is_shutdown_sentinel = false;  // Flag for shutdown coordination

    ThumbnailGenerationTask() = default;
    ThumbnailGenerationTask(const std::string& path, const std::string& h, const ImageInfo& info)
	: file_path(path), hash(h), metadata(info) {}

    // Create shutdown sentinel
    static ThumbnailGenerationTask create_shutdown_sentinel() {
	ThumbnailGenerationTask task;
	task.is_shutdown_sentinel = true;
	return task;
    }
};

struct UIThumbnailTask {
    enum Priority {
	HIGH,   // Visible/soon-to-be-visible images
	LOW     // Background population
    };

    int image_index;
    Priority priority;
    int target_width;
    int target_height;
    std::string hash;
    std::string path;
    uint64_t generation_id = 0;  // For invalidating old high priority requests
    bool is_shutdown_sentinel = false;  // Flag for shutdown coordination

    UIThumbnailTask() = default;
    UIThumbnailTask(int idx, Priority prio, int w, int h, const std::string& hash_val, const std::string &img_path, uint64_t gen_id = 0)
	: image_index(idx), priority(prio), target_width(w), target_height(h), hash(hash_val), path(img_path), generation_id(gen_id) {}

    // Create shutdown sentinel
    static UIThumbnailTask create_shutdown_sentinel() {
	UIThumbnailTask task;
	task.is_shutdown_sentinel = true;
	return task;
    }
};

struct UIDrawTask {
    int image_index;
    std::unique_ptr<Fl_RGB_Image> thumbnail;
    std::string cache_key;
    std::string path;
    bool is_shutdown_sentinel = false;  // Flag for shutdown coordination

    UIDrawTask() = default;
    UIDrawTask(int idx, std::unique_ptr<Fl_RGB_Image> thumb, const std::string& key, const std::string &img_path)
	: image_index(idx), thumbnail(std::move(thumb)), cache_key(key), path(img_path) {}

    // Create shutdown sentinel
    static UIDrawTask create_shutdown_sentinel() {
	UIDrawTask task;
	task.is_shutdown_sentinel = true;
	return task;
    }
};

/**
 * Progress and status reporting
 */
class ProgressReporter {
public:
    using ProgressCallback = std::function<void(int current, int total, const std::string& status)>;

    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    void report_progress(int current, int total, const std::string& status);

private:
    ProgressCallback progress_callback_;
    std::mutex callback_mutex_;
};

/**
 * Directory Scan Thread - coordinates scanning, manages DatabaseManager
 * Now uses centralized TaskScheduler instead of managing its own thread
 */
class DirectoryScanThread {
public:
    DirectoryScanThread();
    ~DirectoryScanThread();

    bool start_scan(const std::string& directory_path, const std::string& db_path);
    void stop_scan();
    void join();

    void set_progress_reporter(std::shared_ptr<ProgressReporter> reporter) {
	progress_reporter_ = reporter;
    }

    DatabaseManager *get_database() const { return database_.get(); }

    // Callback for when image metadata is ready (stage 1)
    using ImageMetadataCallback = std::function<void(const ImageInfo&)>;
    void set_metadata_callback(ImageMetadataCallback callback) {
	metadata_callback_ = callback;
    }

    // Public method to enqueue shutdown sentinels for thumbnail generation workers
    void enqueue_shutdown_thumbnail_task() {
	thumbnail_gen_queue_.enqueue(ThumbnailGenerationTask::create_shutdown_sentinel());
    }

private:
    void scan_task_main();
    void scan_directory_recursive(const std::string& directory);

    std::shared_ptr<TaskScheduler> task_scheduler_;
    std::atomic<bool> should_stop_;

    std::string directory_path_;
    std::string db_path_;
    std::unique_ptr<DatabaseManager> database_;

    std::shared_ptr<ProgressReporter> progress_reporter_;
    ImageMetadataCallback metadata_callback_;

    // Queues for communication with other threads - using BlockingConcurrentQueue for efficient blocking
    moodycamel::BlockingConcurrentQueue<ThumbnailGenerationTask> thumbnail_gen_queue_;

    friend class WorkerPool;
    friend class UpdateMonitorThread;
};

/**
 * Update Monitor Thread - receives progress events from scan thread; notifies UI
 * Now uses centralized TaskScheduler instead of managing its own thread
 */
class UpdateMonitorThread {
public:
    UpdateMonitorThread();
    ~UpdateMonitorThread();

    void start_monitoring(std::shared_ptr<DirectoryScanThread> scan_thread);
    void stop_monitoring();
    void join();

    void set_progress_reporter(std::shared_ptr<ProgressReporter> reporter) {
	progress_reporter_ = reporter;
    }

    // UI notification widget management
    void set_ui_notify_widget(Fl_Widget* widget) {
	ui_notify_widget_ = widget;
    }

private:
    void monitor_task_main();

    std::shared_ptr<TaskScheduler> task_scheduler_;
    std::atomic<bool> should_stop_;

    std::shared_ptr<DirectoryScanThread> scan_thread_;
    std::shared_ptr<ProgressReporter> progress_reporter_;

    // UI notification widget pointer
    Fl_Widget* ui_notify_widget_;
};

/**
 * Worker Pool - loading/generating thumbnails, with handoff to Writer Thread
 * Now uses centralized TaskScheduler instead of managing threads directly
 */
class WorkerPool {
public:
    WorkerPool();
    ~WorkerPool();

    void start_workers(int num_workers = 0);
    void stop_workers();
    void join_all();

    void set_scan_thread(std::shared_ptr<DirectoryScanThread> scan_thread) {
	scan_thread_ = scan_thread;
    }

    // Access to write queue for Writer Thread - using BlockingConcurrentQueue for efficient blocking
    moodycamel::BlockingConcurrentQueue<WriteTask>& get_write_queue() { return write_queue_; }

    // Get worker count for shutdown coordination
    size_t get_worker_count() const;

private:
    void worker_task_main();

    std::shared_ptr<TaskScheduler> task_scheduler_;
    std::atomic<bool> should_stop_;
    std::atomic<size_t> num_workers_;

    std::shared_ptr<DirectoryScanThread> scan_thread_;
    moodycamel::BlockingConcurrentQueue<WriteTask> write_queue_;  // BlockingConcurrentQueue for efficient blocking

    std::atomic<int> processed_count_;
    std::atomic<int> active_workers_;
};

/**
 * Writer Thread - batched database writes, LMDB transactions
 * Now uses centralized TaskScheduler instead of managing its own thread
 */
class WriterThread {
public:
    WriterThread();
    ~WriterThread();

    void start_writing(std::shared_ptr<WorkerPool> worker_pool);
    void stop_writing();
    void join();

private:
    void writer_task_main();

    std::shared_ptr<TaskScheduler> task_scheduler_;
    std::atomic<bool> should_stop_;

    std::shared_ptr<WorkerPool> worker_pool_;
    std::atomic<int> write_count_;
};

/**
 * Thumbnail Workers - UI display generation with priority queues
 * Now uses centralized TaskScheduler instead of managing threads directly
 */
class ThumbnailWorkers {
public:
    ThumbnailWorkers();
    ~ThumbnailWorkers();

    void start_workers(int num_workers = 0);
    void stop_workers();
    void join_all();

    // Queue management
    void enqueue_high_priority(const UIThumbnailTask& task);
    void enqueue_low_priority(const UIThumbnailTask& task);
    bool try_dequeue_result(UIDrawTask& result);

    // High priority queue invalidation for scroll settling
    void flush_high_priority_queue();
    uint64_t get_next_generation_id();
    uint64_t get_current_generation_id() const { return current_generation_id_.load(); }

    void set_database(DatabaseManager *database) { database_ = database; }

    // UI notification widget management
    void set_ui_notify_widget(Fl_Widget* widget) {
	ui_notify_widget_ = widget;
    }

    // Get worker count for shutdown coordination
    size_t get_worker_count() const;

    // Public methods to enqueue shutdown sentinels for clean shutdown
    void enqueue_shutdown_tasks() {
	high_priority_queue_.enqueue(UIThumbnailTask::create_shutdown_sentinel());
	low_priority_queue_.enqueue(UIThumbnailTask::create_shutdown_sentinel());
    }

    void enqueue_shutdown_result() {
	result_queue_.enqueue(UIDrawTask::create_shutdown_sentinel());
    }

private:
    void thumbnail_worker_task_main();
    std::unique_ptr<Fl_RGB_Image> generate_ui_thumbnail(const UIThumbnailTask& task);
    std::unique_ptr<Fl_RGB_Image> create_placeholder_thumbnail(int width, int height, const std::string& message = "Error");

    // Deduplication helper methods
    bool is_request_in_flight(const std::string& cache_key);
    void mark_request_in_flight(const std::string& cache_key);
    void mark_request_completed(const std::string& cache_key);

    std::shared_ptr<TaskScheduler> task_scheduler_;
    std::atomic<bool> should_stop_;
    std::atomic<size_t> num_workers_;

    // Priority queues - using BlockingConcurrentQueue for efficient blocking
    moodycamel::BlockingConcurrentQueue<UIThumbnailTask> high_priority_queue_;
    moodycamel::BlockingConcurrentQueue<UIThumbnailTask> low_priority_queue_;
    moodycamel::BlockingConcurrentQueue<UIDrawTask> result_queue_;

    DatabaseManager *database_ = nullptr;
    std::atomic<int> active_tasks_;
    std::atomic<int> completed_tasks_;

    // UI notification widget pointer
    Fl_Widget* ui_notify_widget_;

    // Deduplication mechanism: Track in-flight thumbnail requests by cache_key (hash:size)
    // Thread-safe: Protected by mutex to prevent race conditions during concurrent access
    std::unordered_set<std::string> in_flight_requests_;
    std::mutex in_flight_mutex_;

    // Generation ID for high priority queue invalidation during scroll settling
    std::atomic<uint64_t> current_generation_id_{0};
};

/**
 * Main Thread Manager - coordinates all threads
 */
class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();

    // Directory scanning
    bool start_directory_scan(const std::string& directory_path, const std::string& db_path = "");
    void cancel_scan();
    void shutdown_all();

    // Progress and callbacks
    void set_progress_callback(ProgressReporter::ProgressCallback callback);
    void set_metadata_callback(DirectoryScanThread::ImageMetadataCallback callback);

    // UI notification widget management
    void set_ui_notify_widget(Fl_Widget* widget) {
	if (monitor_thread_) {
	    monitor_thread_->set_ui_notify_widget(widget);
	}
	if (thumbnail_workers_) {
	    thumbnail_workers_->set_ui_notify_widget(widget);
	}
    }

    // Thumbnail generation for UI
    void request_thumbnail(const UIThumbnailTask& task);
    bool get_thumbnail_result(UIDrawTask& result);

    // High priority queue management for scroll settling
    void flush_high_priority_queue();
    uint64_t get_next_generation_id();

    // Status
    bool is_scanning() const { return GlobalFlags::is_scanning(); }
    bool is_shutdown() const { return GlobalFlags::is_shutdown_requested(); }

private:
    void initialize_threads();
    void cleanup_threads();

    // Thread components
    std::shared_ptr<DirectoryScanThread> scan_thread_;
    std::shared_ptr<UpdateMonitorThread> monitor_thread_;
    std::shared_ptr<WorkerPool> worker_pool_;
    std::shared_ptr<WriterThread> writer_thread_;
    std::shared_ptr<ThumbnailWorkers> thumbnail_workers_;

    std::shared_ptr<ProgressReporter> progress_reporter_;

    bool initialized_;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
