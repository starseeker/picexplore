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
#include <FL/Fl_RGB_Image.H>
#include "database.hpp"
#include "concurrentqueue.h"

namespace fs = std::filesystem;

// Forward declarations
class DatabaseManager;

/**
 * Global shutdown and cancel mechanisms
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
 */
struct ScanTask {
    enum Type {
	SCAN_FILE,
	SCAN_COMPLETE
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

    ThumbnailGenerationTask(const std::string& path, const std::string& h, const ImageInfo& info)
	: file_path(path), hash(h), metadata(info) {}
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

    UIThumbnailTask() = default;
    UIThumbnailTask(int idx, Priority prio, int w, int h, const std::string& hash_val)
	: image_index(idx), priority(prio), target_width(w), target_height(h), hash(hash_val) {}
};

struct UIDrawTask {
    int image_index;
    std::unique_ptr<Fl_RGB_Image> thumbnail;
    std::string cache_key;

    UIDrawTask() = default;
    UIDrawTask(int idx, std::unique_ptr<Fl_RGB_Image> thumb, const std::string& key)
	: image_index(idx), thumbnail(std::move(thumb)), cache_key(key) {}
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

    // Callback for when image metadata is ready (stage 1)
    using ImageMetadataCallback = std::function<void(const ImageInfo&)>;
    void set_metadata_callback(ImageMetadataCallback callback) {
	metadata_callback_ = callback;
    }

private:
    void scan_thread_main();
    void scan_directory_recursive(const std::string& directory);

    std::thread scan_thread_;
    std::atomic<bool> should_stop_;

    std::string directory_path_;
    std::string db_path_;
    std::unique_ptr<DatabaseManager> database_;

    std::shared_ptr<ProgressReporter> progress_reporter_;
    ImageMetadataCallback metadata_callback_;

    // Queues for communication with other threads
    moodycamel::ConcurrentQueue<ThumbnailGenerationTask> thumbnail_gen_queue_;

    friend class WorkerPool;
    friend class UpdateMonitorThread;
};

/**
 * Update Monitor Thread - receives progress events from scan thread; notifies UI
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

private:
    void monitor_thread_main();

    std::thread monitor_thread_;
    std::atomic<bool> should_stop_;

    std::shared_ptr<DirectoryScanThread> scan_thread_;
    std::shared_ptr<ProgressReporter> progress_reporter_;
};

/**
 * Worker Pool - loading/generating thumbnails, with handoff to Writer Thread
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

    // Access to write queue for Writer Thread
    moodycamel::ConcurrentQueue<WriteTask>& get_write_queue() { return write_queue_; }

private:
    void worker_thread_main();

    std::vector<std::thread> worker_threads_;
    std::atomic<bool> should_stop_;

    std::shared_ptr<DirectoryScanThread> scan_thread_;
    moodycamel::ConcurrentQueue<WriteTask> write_queue_;

    std::atomic<int> processed_count_;
    std::atomic<int> active_workers_;
};

/**
 * Writer Thread - batched database writes, LMDB transactions
 */
class WriterThread {
public:
    WriterThread();
    ~WriterThread();

    void start_writing(std::shared_ptr<WorkerPool> worker_pool);
    void stop_writing();
    void join();

private:
    void writer_thread_main();

    std::thread writer_thread_;
    std::atomic<bool> should_stop_;

    std::shared_ptr<WorkerPool> worker_pool_;
    std::atomic<int> write_count_;
};

/**
 * Thumbnail Workers - UI display generation with priority queues
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

    void set_database(std::shared_ptr<DatabaseManager> database) { database_ = database; }

private:
    void thumbnail_worker_thread_main();
    std::unique_ptr<Fl_RGB_Image> generate_ui_thumbnail(const UIThumbnailTask& task);
    std::unique_ptr<Fl_RGB_Image> create_placeholder_thumbnail(int width, int height, const std::string& message = "Error");

    std::vector<std::thread> worker_threads_;
    std::atomic<bool> should_stop_;

    // Priority queues
    moodycamel::ConcurrentQueue<UIThumbnailTask> high_priority_queue_;
    moodycamel::ConcurrentQueue<UIThumbnailTask> low_priority_queue_;
    moodycamel::ConcurrentQueue<UIDrawTask> result_queue_;

    std::shared_ptr<DatabaseManager> database_;
    std::atomic<int> active_tasks_;
    std::atomic<int> completed_tasks_;
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

    // Thumbnail generation for UI
    void request_thumbnail(const UIThumbnailTask& task);
    bool get_thumbnail_result(UIDrawTask& result);

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