/*
 * Fl_JustifiedLayout.h - FLTK widget for displaying thumbnails in justified layout
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

#include <FL/Fl_Widget.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl.H>
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include "../database.h"
#include "../utils.h"
#include "../justified_layout.hpp"
#include "concurrentqueue.h"

// Forward declarations
class DatabaseManager;
class Fl_JustifiedLayout_Content;

// Priority levels for thumbnail generation
enum class ThumbnailPriority {
    HIGH,   // Visible/soon-to-be-visible images
    LOW     // Background population
};

// Thumbnail generation task
struct ThumbnailTask {
    int image_index;
    ThumbnailPriority priority;
    int target_width;
    int target_height;

    ThumbnailTask() = default;
    ThumbnailTask(int idx, ThumbnailPriority prio, int w, int h)
        : image_index(idx), priority(prio), target_width(w), target_height(h) {}
};

// Result of thumbnail generation
struct ThumbnailResult {
    int image_index;
    std::unique_ptr<Fl_RGB_Image> thumbnail;
    std::string cache_key;

    ThumbnailResult() = default;
    ThumbnailResult(int idx, std::unique_ptr<Fl_RGB_Image> thumb, const std::string& key)
        : image_index(idx), thumbnail(std::move(thumb)), cache_key(key) {}
};

// Thumbnail readiness notification
struct ThumbnailNotification {
    std::string hash;
    bool is_ready;

    ThumbnailNotification(const std::string& h, bool ready) : hash(h), is_ready(ready) {}
};

// Batch processing configuration
struct BatchConfig {
    size_t small_batch_threshold = 5;     // Process immediately for small batches
    size_t large_batch_size = 50;         // Process in batches of this size for large operations
    double batch_timeout_ms = 100.0;      // Maximum time to wait before flushing a batch
    bool enable_debug_logging = true;     // Enable detailed batch processing logs
    
    BatchConfig() = default;
};

// Batch processing state for ImageInfo objects
struct ImageInfoBatch {
    std::vector<ImageInfo> pending_images;
    std::chrono::steady_clock::time_point last_batch_time;
    size_t total_images_added = 0;
    size_t total_batches_processed = 0;
    
    ImageInfoBatch() : last_batch_time(std::chrono::steady_clock::now()) {}
};

// Progress callback for background thumbnail generation
using ProgressCallback = std::function<void(int current, int total, const std::string& status)>;

// Selection callback for thumbnail clicks
using SelectionCallback = std::function<void(const std::string& image_path, const ImageInfo& info)>;

/**
 * FLTK widget that displays image thumbnails using justified layout algorithm.
 *
 * Features:
 * - Displays thumbnails for images in justified layout with native scrollbar
 * - API to set LMDB database path
 * - Async thumbnail generation queues with priority support
 * - Progress indication
 * - Selection callbacks for thumbnail interaction
 * - Scrollable view with prefetch support
 */
class Fl_JustifiedLayout : public Fl_Scroll {
    friend class Fl_JustifiedLayout_Content;
public:
    // Constructor
    Fl_JustifiedLayout(int X, int Y, int W, int H, const char* label = nullptr);

    // Destructor
    virtual ~Fl_JustifiedLayout();

    // Database management
    bool set_database_path(const std::string& db_path);
    bool set_directory_path(const std::string& dir_path); // Will scan/build new database

    // Directory scanning control
    void start_directory_scan(const std::string& dir_path, const std::string& db_path = "");
    void cancel_directory_scan();
    bool is_scanning() const { return scanning_.load(); }

    // Two-stage population support  
    using ThumbnailNotificationCallback = std::function<void(const ThumbnailNotification&)>;
    void set_thumbnail_notification_callback(ThumbnailNotificationCallback callback) {
        thumbnail_notification_callback_ = callback;
    }
    void handle_image_info_ready(const ImageInfo& info);  // Stage 1: Add placeholder
    void handle_thumbnail_ready(const std::string& hash);  // Stage 2: Update with thumbnail

    // Batch processing support
    void set_batch_config(const BatchConfig& config) { batch_config_ = config; }
    BatchConfig get_batch_config() const { return batch_config_; }
    
    // Batch processing methods
    void queue_image_info_batch(const ImageInfo& info);
    void flush_pending_image_batch(bool force = false);
    void process_image_info_batch(const std::vector<ImageInfo>& batch);
    
    // Enhanced debug logging
    void log_batch_debug(const std::string& message) const;
    void log_ui_debug(const std::string& message) const;

    // Layout configuration
    void set_row_height(double height) { layout_config_.rh = height; relayout(); }
    void set_spacing(double horizontal, double vertical) {
        layout_config_.sh = horizontal;
        layout_config_.sv = vertical;
        relayout();
    }
    void set_padding(double top, double right, double bottom, double left) {
        layout_config_.pt = top;
        layout_config_.pr = right;
        layout_config_.pb = bottom;
        layout_config_.pl = left;
        relayout();
    }

    // Debug output configuration
    void enable_debug_output(const std::string& output_dir, const std::string& format = "svg") {
        debug_output_enabled_ = true;
        debug_output_dir_ = output_dir;
        debug_output_format_ = format;
        debug_update_counter_ = 0;
    }
    void disable_debug_output() { debug_output_enabled_ = false; }

    // Callback management
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    void set_selection_callback(SelectionCallback callback) { selection_callback_ = callback; }

    // Async thumbnail generation control (stubbed for now)
    void start_background_generation();
    void stop_background_generation();
    bool is_generating() const { return generating_.load(); }

    // Prefetch control
    void prefetch_visible_region();
    void prefetch_next_region();
    void prefetch_previous_region();
    void update_visibility_and_queue_thumbnails();  // Check visibility and queue high-priority tasks

    // FLTK widget overrides
    void draw() override;
    int handle(int event) override;
    void resize(int X, int Y, int W, int H) override;

protected:
    // Internal layout management
    void relayout();
    void calculate_layout();
    void clear_layout();

    // Thumbnail rendering
    void draw_thumbnail_placeholder(int x, int y, int w, int h, const ImageInfo& info);
    void draw_thumbnail_image(int x, int y, int w, int h, const ImageInfo& info);
    void draw_selection_highlight(int x, int y, int w, int h);
    void draw_loading_indicator(int x, int y, int w, int h);  // For images without thumbnails

    // Image decoding and caching
    Fl_RGB_Image* load_thumbnail_image(const ImageInfo& info, int target_width, int target_height);
    void clear_image_cache();

    // Event handling
    void handle_click(int x, int y);

    // Database operations
    bool load_image_list();
    void add_images_incremental(const std::vector<ImageInfo>& new_images);

    // Worker thread methods
    void thumbnail_worker_thread();
    void result_processor_thread();
    int queue_thumbnail_tasks(const std::vector<int>& indices, ThumbnailPriority priority);
    void process_thumbnail_results();
    void process_thumbnail_notifications();  // Process readiness notifications
    static void result_processor_callback(void* data);
    static void progress_update_callback(void* data);
    static void thumbnail_notification_callback(void* data);  // FLTK callback for notifications
    static void batch_flush_callback(void* data);  // FLTK callback for batch processing

    // Directory scanning methods
    void directory_scan_thread(const std::string& dir_path, const std::string& db_path);
    void complete_directory_scan();

    // Debug output methods
    void write_debug_output();  // Write SVG or PNG of current layout
    void write_debug_svg(const std::string& filename);
    void write_debug_png(const std::string& filename);

private:
    // Content widget for scrollable area
    Fl_JustifiedLayout_Content* content_widget_;

    // Database and image management
    std::unique_ptr<DatabaseManager> database_;
    std::vector<ImageInfo> images_;
    std::string current_db_path_;

    // Layout calculation
    LayoutCfg layout_config_;
    std::vector<Item> layout_items_;
    double total_height_;
    int visible_start_idx_;
    int visible_end_idx_;

    // Selection state
    int selected_index_;

    // Async generation state
    std::atomic<bool> generating_;
    std::atomic<bool> should_stop_;

    // Directory scanning state
    std::atomic<bool> scanning_;
    std::atomic<bool> should_cancel_scan_;
    std::thread scan_thread_;

    // Threading for thumbnail generation
    std::vector<std::thread> worker_threads_;
    moodycamel::ConcurrentQueue<ThumbnailTask> high_priority_queue_;
    moodycamel::ConcurrentQueue<ThumbnailTask> low_priority_queue_;
    moodycamel::ConcurrentQueue<ThumbnailResult> result_queue_;
    mutable std::mutex image_cache_mutex_;
    std::atomic<int> active_tasks_;
    std::atomic<int> completed_tasks_;
    std::atomic<int> total_tasks_;

    // Callbacks
    ProgressCallback progress_callback_;
    SelectionCallback selection_callback_;
    ThumbnailNotificationCallback thumbnail_notification_callback_;

    // Image cache for decoded thumbnails
    std::unordered_map<std::string, std::unique_ptr<Fl_RGB_Image>> image_cache_;

    // Two-stage processing support
    moodycamel::ConcurrentQueue<ThumbnailNotification> thumbnail_notifications_;
    std::unordered_map<std::string, int> hash_to_index_map_;  // Map hash to image index

    // Batch processing state
    BatchConfig batch_config_;
    mutable std::mutex batch_mutex_;
    ImageInfoBatch current_batch_;
    std::atomic<bool> batch_flush_scheduled_;
    
    // Enhanced debug logging state
    mutable std::mutex debug_log_mutex_;
    size_t debug_batch_counter_ = 0;

    // Debug output configuration
    bool debug_output_enabled_ = false;
    std::string debug_output_dir_;
    std::string debug_output_format_;  // "svg" or "png"
    int debug_update_counter_ = 0;

    // Constants
    static constexpr int THUMBNAIL_BORDER_WIDTH = 2;
    static constexpr int DEFAULT_ROW_HEIGHT = 150;
    static constexpr int MIN_THUMBNAIL_SIZE = 50;
};

/**
 * Internal content widget that handles the actual drawing of thumbnails.
 * This is managed by the parent Fl_JustifiedLayout (Fl_Scroll) widget.
 */
class Fl_JustifiedLayout_Content : public Fl_Widget {
public:
    Fl_JustifiedLayout_Content(int X, int Y, int W, int H, Fl_JustifiedLayout* parent);
    void draw() override;
    int handle(int event) override;

private:
    Fl_JustifiedLayout* parent_;
};
