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

    // Callback management
    void set_progress_callback(ProgressCallback callback) { progress_callback_ = callback; }
    void set_selection_callback(SelectionCallback callback) { selection_callback_ = callback; }

    // Async thumbnail generation control (stubbed for now)
    void start_background_generation();
    void stop_background_generation();
    bool is_generating() const { return generating_.load(); }

    // Prefetch control (stubbed for now)
    void prefetch_visible_region();
    void prefetch_next_region();
    void prefetch_previous_region();

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
    static void result_processor_callback(void* data);
    static void progress_update_callback(void* data);

    // Directory scanning methods
    void directory_scan_thread(const std::string& dir_path, const std::string& db_path);
    void complete_directory_scan();

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

    // Image cache for decoded thumbnails
    std::unordered_map<std::string, std::unique_ptr<Fl_RGB_Image>> image_cache_;

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
