/*
 * Fl_JustifiedLayout.cpp - FLTK widget for displaying thumbnails in justified layout
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "Fl_JustifiedLayout.h"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <chrono>
#include <thread>
#include <filesystem>
#include "stb_image.h"
#include "../utils.h"
#include "../simple_svg_1.0.0.hpp"

// Helper function to wrap Fl::awake with debug logging
inline void debug_awake(void (*callback)(void*), void* data, const std::string& description = "") {
    std::cout << "[DEBUG] FLTK: Triggering Fl::awake() from thread " << std::this_thread::get_id();
    if (!description.empty()) {
        std::cout << " - " << description;
    }
    std::cout << std::endl;
    
    Fl::awake(callback, data);
    
    std::cout << "[DEBUG] FLTK: Fl::awake() call completed" << std::endl;
}

Fl_JustifiedLayout::Fl_JustifiedLayout(int X, int Y, int W, int H, const char* label)
    : Fl_Scroll(X, Y, W, H, label)
    , content_widget_(nullptr)
    , database_(nullptr)
    , layout_config_()
    , total_height_(0)
    , visible_start_idx_(0)
    , visible_end_idx_(0)
    , selected_index_(-1)
    , generating_(false)
    , should_stop_(false)
    , scanning_(false)
    , should_cancel_scan_(false)
    , active_tasks_(0)
    , completed_tasks_(0)
    , total_tasks_(0)
    , thumbnail_notification_callback_(nullptr)
    , batch_flush_scheduled_(false)
{
    // Initialize layout configuration with reasonable defaults
    layout_config_.w = W - 20; // Leave some margin for scrollbar
    layout_config_.rh = DEFAULT_ROW_HEIGHT;
    layout_config_.pt = layout_config_.pr = layout_config_.pb = layout_config_.pl = 10;
    layout_config_.sh = layout_config_.sv = 5;

    // Set widget color scheme
    color(FL_WHITE);
    selection_color(FL_BLUE);

    // Configure scrollbar behavior
    type(Fl_Scroll::VERTICAL);  // Only vertical scrolling

    // Create content widget - initially small, will be resized in relayout()
    content_widget_ = new Fl_JustifiedLayout_Content(X, Y, W, 100, this);
    end(); // Important: end() to finalize the Fl_Scroll's children
}

// Helper to decode and create a Fl_RGB_Image for worker thread
static std::unique_ptr<Fl_RGB_Image> worker_decode_fl_rgb_image(const ImageInfo& info, int target_width, int target_height) {
    // Check if we have thumbnail data
    if (info.thumb_data.empty()) {
        return nullptr;
    }

    // Decode image from memory using stb_image
    int image_width, image_height, channels;
    unsigned char* pixels = stbi_load_from_memory(
        info.thumb_data.data(),
        static_cast<int>(info.thumb_data.size()),
        &image_width, &image_height, &channels, 3  // Force RGB
    );

    if (!pixels) {
        std::cerr << "Failed to decode thumbnail for " << info.path << ": " << stbi_failure_reason() << std::endl;
        return nullptr;
    }

    // Calculate scaled dimensions while maintaining aspect ratio
    double aspect_ratio = static_cast<double>(image_width) / image_height;
    int scaled_width, scaled_height;

    if (static_cast<double>(target_width) / target_height > aspect_ratio) {
        // Target is wider than image - fit to height
        scaled_height = target_height;
        scaled_width = static_cast<int>(target_height * aspect_ratio);
    } else {
        // Target is taller than image - fit to width
        scaled_width = target_width;
        scaled_height = static_cast<int>(target_width / aspect_ratio);
    }

    // Ensure minimum size
    if (scaled_width < 1) scaled_width = 1;
    if (scaled_height < 1) scaled_height = 1;

    unsigned char* scaled_pixels = nullptr;

    // Scale image if needed
    if (scaled_width != image_width || scaled_height != image_height) {
        scaled_pixels = new unsigned char[scaled_width * scaled_height * 3];

        // Simple nearest-neighbor scaling for now
        for (int y = 0; y < scaled_height; y++) {
            for (int x = 0; x < scaled_width; x++) {
                int src_x = (x * image_width) / scaled_width;
                int src_y = (y * image_height) / scaled_height;

                // Ensure bounds
                if (src_x >= image_width) src_x = image_width - 1;
                if (src_y >= image_height) src_y = image_height - 1;

                int src_idx = (src_y * image_width + src_x) * 3;
                int dst_idx = (y * scaled_width + x) * 3;

                scaled_pixels[dst_idx] = pixels[src_idx];
                scaled_pixels[dst_idx + 1] = pixels[src_idx + 1];
                scaled_pixels[dst_idx + 2] = pixels[src_idx + 2];
            }
        }

        stbi_image_free(pixels);
        pixels = scaled_pixels;
    } else {
        scaled_pixels = pixels;
    }

    // FLTK will take ownership of the pixel data, so we create a copy
    unsigned char* fltk_pixels = new unsigned char[scaled_width * scaled_height * 3];
    std::memcpy(fltk_pixels, scaled_pixels, scaled_width * scaled_height * 3);

    if (scaled_pixels != pixels) {
        delete[] scaled_pixels;
    } else {
        stbi_image_free(pixels);
    }

    return std::make_unique<Fl_RGB_Image>(fltk_pixels, scaled_width, scaled_height, 3);
}

void Fl_JustifiedLayout::thumbnail_worker_thread() {
    log_ui_debug("Thumbnail worker thread " + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + " started");

    ThumbnailTask task;
    int tasks_processed = 0;

    while (!should_stop_.load()) {
        bool found_task = false;

        // Try high priority queue first
        if (high_priority_queue_.try_dequeue(task)) {
            found_task = true;
            log_ui_debug("Worker dequeued HIGH priority task for image " + std::to_string(task.image_index) +
                         " (" + std::to_string(task.target_width) + "x" + std::to_string(task.target_height) + ")");
        }
        // Fall back to low priority queue
        else if (low_priority_queue_.try_dequeue(task)) {
            found_task = true;
            log_ui_debug("Worker dequeued LOW priority task for image " + std::to_string(task.image_index) +
                         " (" + std::to_string(task.target_width) + "x" + std::to_string(task.target_height) + ")");
        }

        if (found_task) {
            active_tasks_.fetch_add(1);
            tasks_processed++;

            if (task.image_index >= 0 && task.image_index < static_cast<int>(images_.size())) {
                const auto& img_info = images_[task.image_index];

                // Create cache key
                std::string cache_key = img_info.hash + "_" +
                                      std::to_string(task.target_width) + "x" +
                                      std::to_string(task.target_height);

                // Check cache before generating thumbnail (avoid duplicate work)
                {
                    std::lock_guard<std::mutex> lock(image_cache_mutex_);
                    if (image_cache_.find(cache_key) != image_cache_.end()) {
                        // Thumbnail already cached
                        active_tasks_.fetch_sub(1);
                        completed_tasks_.fetch_add(1);
                        continue;
                    }
                }

                std::unique_ptr<Fl_RGB_Image> thumbnail;

                // Check if image has thumbnails in database
                if (img_info.has_thumbnails) {
                    // Load from existing thumbnail data
                    thumbnail = worker_decode_fl_rgb_image(img_info, task.target_width, task.target_height);
                } else {
                    // Generate thumbnail from source image using database
                    if (database_) {
                        // Generate thumbnails for this hash
                        if (database_->generate_thumbnails_for_hash(img_info.hash, img_info.path)) {
                            // Reload the image info to get the thumbnails
                            ImageInfo updated_img = img_info;
                            MDB_txn* read_txn;
                            if (database_->begin_read_transaction(read_txn)) {
                                if (database_->load_image_info(read_txn, img_info.hash, updated_img)) {
                                    // Decode the thumbnail
                                    thumbnail = worker_decode_fl_rgb_image(updated_img, task.target_width, task.target_height);

                                    // Update the main image list
                                    {
                                        std::lock_guard<std::mutex> lock(image_cache_mutex_);
                                        if (task.image_index < static_cast<int>(images_.size())) {
                                            images_[task.image_index] = updated_img;
                                        }
                                    }

                                    // Notify UI that thumbnail is ready
                                    handle_thumbnail_ready(img_info.hash);
                                }
                                database_->abort_transaction(read_txn);
                            }
                        }
                    }
                }

                if (thumbnail) {
                    log_ui_debug("Thumbnail created for image " + std::to_string(task.image_index) + ", enqueueing result");
                    ThumbnailResult result(task.image_index, std::move(thumbnail), cache_key);
                    result_queue_.enqueue(std::move(result));
                    debug_awake(result_processor_callback, this, "thumbnail generation result");
                }
            }

            active_tasks_.fetch_sub(1);
            completed_tasks_.fetch_add(1);
        }
        else {
            // No tasks available, sleep briefly  
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    log_ui_debug("Thumbnail worker thread shutting down after processing " + std::to_string(tasks_processed) + " tasks");
}

void Fl_JustifiedLayout::result_processor_callback(void* data) {
    if (data) {
        static_cast<Fl_JustifiedLayout*>(data)->process_thumbnail_results();
    }
}

void Fl_JustifiedLayout::progress_update_callback(void* data) {
    if (data) {
        Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
        if (widget->generating_.load() && widget->content_widget_) {
            widget->content_widget_->redraw();
            // Schedule next update
            Fl::add_timeout(0.1, progress_update_callback, data);
        }
    }
}

void Fl_JustifiedLayout::process_thumbnail_results() {
    static std::atomic_flag processing = ATOMIC_FLAG_INIT;
    if (processing.test_and_set()) {
        return;  // Avoid re-entry
    }

    ThumbnailResult result;
    bool any_processed = false;
    int results_processed = 0;

    // Process all available results
    while (result_queue_.try_dequeue(result)) {
        results_processed++;

        if (result.thumbnail) {
            std::lock_guard<std::mutex> lock(image_cache_mutex_);
            if (image_cache_.find(result.cache_key) != image_cache_.end()) {
                continue; // Already in cache, skip duplicate job
            }
            image_cache_[result.cache_key] = std::move(result.thumbnail);
            any_processed = true;
        }
    }

    log_batch_debug("Processed " + std::to_string(results_processed) + " thumbnail results, any_processed: " + 
                   (any_processed ? "true" : "false"));

    // Trigger redraw if any thumbnails were processed
    if (any_processed) {
        if (content_widget_) {
            content_widget_->redraw();
        }

        // Update progress if callback is set
        if (progress_callback_) {
            int completed = completed_tasks_.load();
            int total = total_tasks_.load();
            progress_callback_(completed, total, "Generating thumbnails...");
        }
    }

    processing.clear();
}

int Fl_JustifiedLayout::queue_thumbnail_tasks(const std::vector<int>& indices, ThumbnailPriority priority) {
	int num_queued = 0;
    for (int idx : indices) {
        if (idx >= 0 && idx < static_cast<int>(images_.size()) && idx < static_cast<int>(layout_items_.size())) {
            const auto& item = layout_items_[idx];
            const auto& img = images_[idx];

            // Skip images that already have thumbnails
            if (img.has_thumbnails) {
                continue;
            }

            int target_w = static_cast<int>(item.w) - 2 * THUMBNAIL_BORDER_WIDTH;
            int target_h = static_cast<int>(item.h) - 2 * THUMBNAIL_BORDER_WIDTH;

            // Check if we already have a cached version
            std::string cache_key = img.hash + "_" +
                std::to_string(target_w) + "x" + std::to_string(target_h);
            {
                std::lock_guard<std::mutex> lock(image_cache_mutex_);
                if (image_cache_.find(cache_key) != image_cache_.end()) {
                    continue; // Already cached, do not queue
                }
            }

            // Queue thumbnail generation task for this image
            ThumbnailTask task(idx, priority, target_w, target_h);

            if (priority == ThumbnailPriority::HIGH) {
                high_priority_queue_.enqueue(task);
            } else {
                low_priority_queue_.enqueue(task);
            }

            total_tasks_.fetch_add(1);
            num_queued++;
        }
    }
    
    if (num_queued > 0) {
        log_ui_debug("Queued " + std::to_string(num_queued) + " " + 
                    (priority == ThumbnailPriority::HIGH ? "HIGH" : "LOW") + 
                    " priority thumbnail tasks, total tasks now: " + std::to_string(total_tasks_.load()));
    }
    
    return num_queued;
}

Fl_JustifiedLayout::~Fl_JustifiedLayout() {
    // Cancel any pending batch flush timers
    if (batch_flush_scheduled_.load()) {
        Fl::remove_timeout(batch_flush_callback, this);
        batch_flush_scheduled_.store(false);
    }
    
    // Flush any remaining batch before shutdown
    flush_pending_image_batch(true);
    
    cancel_directory_scan();
    stop_background_generation();
    clear_image_cache();
}

bool Fl_JustifiedLayout::set_database_path(const std::string& db_path) {
    // Stop any ongoing generation first
    stop_background_generation();

    current_db_path_ = db_path;

    // Create and initialize database manager
    database_ = std::make_unique<DatabaseManager>();

    // Set up callback for two-stage processing
    database_->set_image_info_callback([this](const ImageInfo& info) {
        // This callback is called from worker threads, so we need to use Fl::awake
        std::cout << "[DEBUG] Worker thread " << std::this_thread::get_id() << " image info callback triggered for: "
                  << info.path << " (hash: " << info.hash << ")" << std::endl;
        auto info_copy = std::make_shared<ImageInfo>(info);
        debug_awake([](void* data) {
            std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " handling image info ready callback (from database)" << std::endl;
            auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>*>(data);
            params->first->handle_image_info_ready(*(params->second));
            delete params;
        }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>(this, info_copy), "image info ready from database");
    });

    // Try to open the database
    if (!database_->open(db_path)) {
        std::cerr << "Failed to open database: " << db_path << std::endl;
        database_.reset();
        return false;
    }

    // Load image list
    return load_image_list();
}

bool Fl_JustifiedLayout::set_directory_path(const std::string& dir_path) {
    // Cancel any ongoing operations
    cancel_directory_scan();
    stop_background_generation();

    // Use common cache database path instead of directory-specific path
    std::string db_path = get_cache_db_path();

    // Start asynchronous directory scan
    start_directory_scan(dir_path, db_path);

    return true;
}

void Fl_JustifiedLayout::start_background_generation() {
    if (generating_.load()) return;

    generating_.store(true);
    should_stop_.store(false);

    // Clear previous state
    completed_tasks_.store(0);
    total_tasks_.store(0);

    // Start worker threads (use half of available cores, minimum 1, maximum 4)
    int num_workers = std::max(1, std::min(4, static_cast<int>(std::thread::hardware_concurrency() / 2)));

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " starting " << num_workers
              << " thumbnail worker threads" << std::endl;

    worker_threads_.clear();
    worker_threads_.reserve(num_workers);

    for (int i = 0; i < num_workers; ++i) {
        std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " launching thumbnail worker thread #"
                  << (i + 1) << std::endl;
        worker_threads_.emplace_back(&Fl_JustifiedLayout::thumbnail_worker_thread, this);
    }

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " Started background thumbnail generation with "
              << num_workers << " workers" << std::endl;

    // Queue high priority tasks for visible region
    prefetch_visible_region();

    // Queue low priority tasks for all images
    std::vector<int> all_indices;
    for (int i = 0; i < static_cast<int>(images_.size()); ++i) {
        all_indices.push_back(i);
    }
    queue_thumbnail_tasks(all_indices, ThumbnailPriority::LOW);

    if (progress_callback_) {
        progress_callback_(0, total_tasks_.load(), "Starting background generation...");
    }

    // Start periodic progress updates
    Fl::add_timeout(0.1, progress_update_callback, this);
}

void Fl_JustifiedLayout::stop_background_generation() {
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " stopping background thumbnail generation" << std::endl;
    should_stop_.store(true);

    // Wait for all worker threads to complete
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " waiting for " << worker_threads_.size()
              << " worker threads to join" << std::endl;
    for (size_t i = 0; i < worker_threads_.size(); ++i) {
        auto& thread = worker_threads_[i];
        if (thread.joinable()) {
            std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " joining worker thread #" << (i + 1) << std::endl;
            thread.join();
        }
    }
    worker_threads_.clear();
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " all worker threads joined" << std::endl;

    // Clear queues
    ThumbnailTask task;
    int high_cleared = 0, low_cleared = 0;
    while (high_priority_queue_.try_dequeue(task)) { high_cleared++; }
    while (low_priority_queue_.try_dequeue(task)) { low_cleared++; }

    ThumbnailResult result;
    int results_cleared = 0;
    while (result_queue_.try_dequeue(result)) { results_cleared++; }

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " cleared " << high_cleared << " high priority tasks, "
              << low_cleared << " low priority tasks, and " << results_cleared << " results from queues" << std::endl;

    generating_.store(false);

    // Remove any pending progress update timers
    Fl::remove_timeout(progress_update_callback, this);

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " Stopped background thumbnail generation" << std::endl;
}

void Fl_JustifiedLayout::prefetch_visible_region() {
    if (!generating_.load() || layout_items_.empty()) return;

    // Queue visible region for high priority processing
    std::vector<int> visible_indices;
    for (int i = visible_start_idx_; i <= visible_end_idx_; ++i) {
        visible_indices.push_back(i);
    }

    if (!visible_indices.empty()) {
	int num_queued = queue_thumbnail_tasks(visible_indices, ThumbnailPriority::HIGH);
        if (num_queued > 0) {
            std::cout << "Queued visible region for high priority: " << visible_start_idx_ << " to " << visible_end_idx_ << std::endl;
        }
    }
}

void Fl_JustifiedLayout::prefetch_next_region() {
    if (!generating_.load() || layout_items_.empty()) return;

    // Calculate and prefetch next page of thumbnails
    int next_start = visible_end_idx_ + 1;
    int next_end = std::min(next_start + 20, static_cast<int>(images_.size()) - 1);

    if (next_start <= next_end) {
        std::vector<int> next_indices;
        for (int i = next_start; i <= next_end; ++i) {
            next_indices.push_back(i);
        }
        queue_thumbnail_tasks(next_indices, ThumbnailPriority::HIGH);
        std::cout << "Queued next region for high priority: " << next_start << " to " << next_end << std::endl;
    }
}

void Fl_JustifiedLayout::prefetch_previous_region() {
    if (!generating_.load() || layout_items_.empty()) return;

    // Calculate and prefetch previous page of thumbnails
    int prev_end = visible_start_idx_ - 1;
    int prev_start = std::max(prev_end - 20, 0);

    if (prev_start <= prev_end) {
        std::vector<int> prev_indices;
        for (int i = prev_start; i <= prev_end; ++i) {
            prev_indices.push_back(i);
        }
        queue_thumbnail_tasks(prev_indices, ThumbnailPriority::HIGH);
        std::cout << "Queued previous region for high priority: " << prev_start << " to " << prev_end << std::endl;
    }
}

int Fl_JustifiedLayout::handle(int event) {
    std::cout << "[DEBUG] FLTK UI: Fl_JustifiedLayout::handle() event=" << event << " on thread " << std::this_thread::get_id() << std::endl;
    
    switch (event) {
        case FL_FOCUS:
        case FL_UNFOCUS:
            std::cout << "[DEBUG] FLTK UI: Focus event handled" << std::endl;
            return 1;
        case FL_MOVE:
        case FL_DRAG:
            std::cout << "[DEBUG] FLTK UI: Mouse move/drag event - updating visibility" << std::endl;
            // Check for scroll position changes
            update_visibility_and_queue_thumbnails();
            break;
        case FL_PUSH:
            std::cout << "[DEBUG] FLTK UI: Mouse push event" << std::endl;
            break;
        case FL_RELEASE:
            std::cout << "[DEBUG] FLTK UI: Mouse release event" << std::endl;
            break;
        case FL_KEYDOWN:
        case FL_KEYUP:
            std::cout << "[DEBUG] FLTK UI: Keyboard event" << std::endl;
            break;
    }

    // Let Fl_Scroll handle scrolling and other events
    // Click events are handled by the content widget
    int result = Fl_Scroll::handle(event);

    // After any scroll handling, update visibility
    if (event == FL_MOUSEWHEEL || event == FL_PUSH || event == FL_DRAG) {
        update_visibility_and_queue_thumbnails();
    }

    return result;
}

void Fl_JustifiedLayout::resize(int X, int Y, int W, int H) {
    Fl_Scroll::resize(X, Y, W, H);

    // Update layout width accounting for scrollbar
    layout_config_.w = W - 20;

    // Relayout will trigger new thumbnail generation if needed
    relayout();

    // If generation is active, queue visible region for high priority
    if (generating_.load()) {
        prefetch_visible_region();
    }
}

void Fl_JustifiedLayout::relayout() {
    if (images_.empty()) {
        if (content_widget_) {
            content_widget_->resize(x(), y(), w(), h());
        }
        redraw();
        return;
    }

    clear_layout();
    clear_image_cache();  // Clear cache when layout changes
    calculate_layout();

    // Resize content widget to match the total layout height
    if (content_widget_) {
        int content_height = std::max(static_cast<int>(total_height_), h());
        content_widget_->resize(x(), y(), w(), content_height);
    }

    redraw();
}

void Fl_JustifiedLayout::calculate_layout() {
    if (images_.empty()) return;

    // Always recalculate layout when called - this ensures correctness
    // for incremental updates. For better performance, we could optimize
    // this later to only recalculate changed portions.
    layout_items_.clear();

    // Convert images to layout input format
    std::vector<Item> input_items;
    for (const auto& img : images_) {
        Item item;
        item.ar = img.aspect_ratio;
        input_items.push_back(item);
    }

    // Calculate justified layout
    JustifiedLayout layout(input_items, layout_config_);
    layout_items_ = layout.boxes();
    total_height_ = layout.height();

    // Calculate visible range based on Fl_Scroll's current position
    visible_start_idx_ = 0;
    visible_end_idx_ = static_cast<int>(layout_items_.size()) - 1;

    // Get the current scroll position from Fl_Scroll
    int scroll_y = yposition();
    int viewport_height = h();

    // After layout calculation, check visibility and queue thumbnails
    update_visibility_and_queue_thumbnails();

    // Visibility calculation
    bool found_first = false;
    for (size_t i = 0; i < layout_items_.size(); ++i) {
        const auto& item = layout_items_[i];
        int item_top = static_cast<int>(item.t);
        int item_bottom = item_top + static_cast<int>(item.h);

        // Check if item intersects with visible viewport
        if (item_bottom >= scroll_y && item_top <= scroll_y + viewport_height) {
            if (!found_first) {
                visible_start_idx_ = static_cast<int>(i);
                found_first = true;
            }
            visible_end_idx_ = static_cast<int>(i);
        }
    }

    // Trigger prefetching when visible region is calculated
    if (generating_.load()) {
        prefetch_visible_region();
    }

    // Write debug output if enabled
    if (debug_output_enabled_) {
        write_debug_output();
    }
}

void Fl_JustifiedLayout::clear_layout() {
    layout_items_.clear();
    total_height_ = 0;
}

void Fl_JustifiedLayout::clear_image_cache() {
    std::lock_guard<std::mutex> lock(image_cache_mutex_);
    image_cache_.clear();
}

Fl_RGB_Image* Fl_JustifiedLayout::load_thumbnail_image(const ImageInfo& info, int target_width, int target_height) {
    // Check if we have thumbnail data
    if (info.thumb_data.empty()) {
        return nullptr;
    }

    // Create cache key based on hash and target dimensions
    std::string cache_key = info.hash + "_" + std::to_string(target_width) + "x" + std::to_string(target_height);

    // Check cache first (thread-safe)
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex_);
        auto cache_it = image_cache_.find(cache_key);
        if (cache_it != image_cache_.end()) {
            return cache_it->second.get();
        }
    }

    // Decode image from memory using stb_image
    int image_width, image_height, channels;
    unsigned char* pixels = stbi_load_from_memory(
        info.thumb_data.data(),
        static_cast<int>(info.thumb_data.size()),
        &image_width, &image_height, &channels, 3  // Force RGB
    );

    if (!pixels) {
        std::cerr << "Failed to decode thumbnail for " << info.path << ": " << stbi_failure_reason() << std::endl;
        return nullptr;
    }

    // Calculate scaled dimensions while maintaining aspect ratio
    double aspect_ratio = static_cast<double>(image_width) / image_height;
    int scaled_width, scaled_height;

    if (static_cast<double>(target_width) / target_height > aspect_ratio) {
        // Target is wider than image - fit to height
        scaled_height = target_height;
        scaled_width = static_cast<int>(target_height * aspect_ratio);
    } else {
        // Target is taller than image - fit to width
        scaled_width = target_width;
        scaled_height = static_cast<int>(target_width / aspect_ratio);
    }

    // Ensure minimum size
    if (scaled_width < 1) scaled_width = 1;
    if (scaled_height < 1) scaled_height = 1;

    unsigned char* scaled_pixels = nullptr;

    // Scale image if needed
    if (scaled_width != image_width || scaled_height != image_height) {
        scaled_pixels = new unsigned char[scaled_width * scaled_height * 3];

        // Simple nearest-neighbor scaling for now
        for (int y = 0; y < scaled_height; y++) {
            for (int x = 0; x < scaled_width; x++) {
                int src_x = (x * image_width) / scaled_width;
                int src_y = (y * image_height) / scaled_height;

                // Ensure bounds
                if (src_x >= image_width) src_x = image_width - 1;
                if (src_y >= image_height) src_y = image_height - 1;

                int src_idx = (src_y * image_width + src_x) * 3;
                int dst_idx = (y * scaled_width + x) * 3;

                scaled_pixels[dst_idx] = pixels[src_idx];
                scaled_pixels[dst_idx + 1] = pixels[src_idx + 1];
                scaled_pixels[dst_idx + 2] = pixels[src_idx + 2];
            }
        }

        stbi_image_free(pixels);
        pixels = scaled_pixels;
    } else {
        scaled_pixels = pixels;
    }

    // Create FLTK RGB image
    // FLTK will take ownership of the pixel data, so we create a copy
    unsigned char* fltk_pixels = new unsigned char[scaled_width * scaled_height * 3];
    std::memcpy(fltk_pixels, scaled_pixels, scaled_width * scaled_height * 3);

    if (scaled_pixels != pixels) {
        delete[] scaled_pixels;
    } else {
        stbi_image_free(pixels);
    }

    auto fltk_image = std::make_unique<Fl_RGB_Image>(fltk_pixels, scaled_width, scaled_height, 3);
    Fl_RGB_Image* result = fltk_image.get();

    // Cache the image (thread-safe)
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex_);
        image_cache_[cache_key] = std::move(fltk_image);
    }

    return result;
}

void Fl_JustifiedLayout::draw_thumbnail_image(int x, int y, int w, int h, const ImageInfo& info) {
    // Try to load the real thumbnail image
    Fl_RGB_Image* thumb_image = load_thumbnail_image(info, w - 2 * THUMBNAIL_BORDER_WIDTH, h - 2 * THUMBNAIL_BORDER_WIDTH);

    if (thumb_image) {
        // Draw border
        fl_color(FL_GRAY);
        fl_rect(x, y, w, h);

        // Fill background
        fl_color(FL_WHITE);
        fl_rectf(x + THUMBNAIL_BORDER_WIDTH, y + THUMBNAIL_BORDER_WIDTH,
                w - 2 * THUMBNAIL_BORDER_WIDTH, h - 2 * THUMBNAIL_BORDER_WIDTH);

        // Center the image within the allocated space
        int img_w = thumb_image->w();
        int img_h = thumb_image->h();
        int img_x = x + THUMBNAIL_BORDER_WIDTH + (w - 2 * THUMBNAIL_BORDER_WIDTH - img_w) / 2;
        int img_y = y + THUMBNAIL_BORDER_WIDTH + (h - 2 * THUMBNAIL_BORDER_WIDTH - img_h) / 2;

        // Draw the image
        thumb_image->draw(img_x, img_y);
    } else {
        // Fallback to placeholder rendering
        draw_thumbnail_placeholder(x, y, w, h, info);
    }
}

void Fl_JustifiedLayout::draw_thumbnail_placeholder(int x, int y, int w, int h, const ImageInfo& info) {
    // Check if this thumbnail is being generated
    bool is_loading = generating_.load();

    // Draw outer border with different color based on state
    if (is_loading) {
        fl_color(FL_BLUE);  // Blue border when thumbnails are being generated
    } else {
        fl_color(FL_DARK2); // Darker border for newly discovered images
    }
    fl_rect(x, y, w, h);

    // Fill interior with light gray background
    fl_color(FL_LIGHT2);
    fl_rectf(x + THUMBNAIL_BORDER_WIDTH, y + THUMBNAIL_BORDER_WIDTH,
            w - 2 * THUMBNAIL_BORDER_WIDTH, h - 2 * THUMBNAIL_BORDER_WIDTH);

    // Draw placeholder content with dark text
    fl_color(FL_DARK3);
    fl_font(FL_HELVETICA, 10);

    // Draw image info text (filename)
    std::string filename = info.path;
    size_t pos = filename.find_last_of('/');
    if (pos != std::string::npos) {
        filename = filename.substr(pos + 1);
    }

    // Truncate filename if too long
    if (filename.length() > 20) {
        filename = filename.substr(0, 17) + "...";
    }

    int text_w = 0, text_h = 0;
    fl_measure(filename.c_str(), text_w, text_h);
    int text_y = y + h / 2 - 15;

    // Show loading indicator if generating
    if (is_loading) {
        fl_color(FL_BLUE);
        int loading_w = 0, loading_h = 0;
        const char* loading_text = "[ Loading... ]";
        fl_measure(loading_text, loading_w, loading_h);
        fl_draw(loading_text, x + (w - loading_w) / 2, text_y);
        fl_color(FL_DARK3);
        text_y += 15;
    } else {
        // Show "New Image" indicator for newly discovered images
        fl_color(FL_DARK_GREEN);
        int new_w = 0, new_h = 0;
        const char* new_text = "[ New Image ]";
        fl_measure(new_text, new_w, new_h);
        fl_draw(new_text, x + (w - new_w) / 2, text_y);
        fl_color(FL_DARK3);
        text_y += 15;
    }

    // Draw filename
    fl_draw(filename.c_str(), x + (w - text_w) / 2, text_y);

    // Draw aspect ratio and dimensions info
    fl_font(FL_HELVETICA, 8);
    char info_str[64];
    snprintf(info_str, sizeof(info_str), "%.2f:1 ratio", info.aspect_ratio);
    int info_w = 0, info_h = 0;
    fl_measure(info_str, info_w, info_h);
    fl_draw(info_str, x + (w - info_w) / 2, text_y + 15);

    // Draw approximate dimensions based on current box size
    char size_str[64];
    int est_width = static_cast<int>(h * info.aspect_ratio);
    int est_height = h;
    snprintf(size_str, sizeof(size_str), "~%dx%d px", est_width, est_height);
    int size_w = 0, size_h = 0;
    fl_measure(size_str, size_w, size_h);
    fl_draw(size_str, x + (w - size_w) / 2, text_y + 27);
}

void Fl_JustifiedLayout::draw_selection_highlight(int x, int y, int w, int h) {
    fl_color(selection_color());
    fl_rect(x - 2, y - 2, w + 4, h + 4);
    fl_rect(x - 1, y - 1, w + 2, h + 2);
}

void Fl_JustifiedLayout::handle_click(int click_x, int click_y) {
    if (!content_widget_) return;

    // Convert click coordinates to content widget-relative
    int rel_x = click_x - content_widget_->x();
    int rel_y = click_y - content_widget_->y();

    // Find clicked thumbnail
    for (size_t i = visible_start_idx_; i <= visible_end_idx_ && i < layout_items_.size(); ++i) {
        const auto& item = layout_items_[i];

        int item_x = static_cast<int>(item.l);
        int item_y = static_cast<int>(item.t);
        int item_w = static_cast<int>(item.w);
        int item_h = static_cast<int>(item.h);

        if (rel_x >= item_x && rel_x < item_x + item_w &&
            rel_y >= item_y && rel_y < item_y + item_h) {

            // Update selection
            selected_index_ = static_cast<int>(i);
            redraw();

            // Call selection callback
            if (selection_callback_ && i < images_.size()) {
                selection_callback_(images_[i].path, images_[i]);
            }

            std::cout << "Selected image: " << images_[i].path << std::endl;
            break;
        }
    }
}

// Fl_JustifiedLayout_Content implementation
Fl_JustifiedLayout_Content::Fl_JustifiedLayout_Content(int X, int Y, int W, int H, Fl_JustifiedLayout* parent)
    : Fl_Widget(X, Y, W, H), parent_(parent) {
}

void Fl_JustifiedLayout_Content::draw() {
    std::cout << "[DEBUG] FLTK UI: Fl_JustifiedLayout_Content::draw() called on thread " << std::this_thread::get_id() << std::endl;
    
    if (!parent_) return;

    // Clear background
    fl_color(parent_->color());
    fl_rectf(x(), y(), w(), h());

    if (parent_->images_.empty()) {
        std::cout << "[DEBUG] FLTK UI: Drawing 'no images' message" << std::endl;
        // Draw "no images" message
        fl_color(FL_BLACK);
        fl_font(FL_HELVETICA, 14);
        const char* msg = "No images to display. Set database or directory path.";
        int tw = 0, th = 0;
        fl_measure(msg, tw, th);
        fl_draw(msg, x() + (w() - tw) / 2, y() + (h() - th) / 2);
        return;
    }

    // Calculate visible items based on current scroll position
    parent_->calculate_layout();

    // Draw visible thumbnails
    fl_push_clip(x(), y(), w(), h());

    for (size_t i = parent_->visible_start_idx_; i <= parent_->visible_end_idx_ && i < parent_->layout_items_.size(); ++i) {
        const auto& item = parent_->layout_items_[i];
        const auto& img = parent_->images_[i];

        int item_x = x() + static_cast<int>(item.l);
        int item_y = y() + static_cast<int>(item.t);
        int item_w = static_cast<int>(item.w);
        int item_h = static_cast<int>(item.h);

        // Skip items completely outside visible area
        if (item_y + item_h < y() || item_y > y() + h()) continue;

        // Draw selection highlight if selected
        if (static_cast<int>(i) == parent_->selected_index_) {
            parent_->draw_selection_highlight(item_x, item_y, item_w, item_h);
        }

        // Check if image has thumbnails
        if (img.has_thumbnails) {
            parent_->draw_thumbnail_image(item_x, item_y, item_w, item_h, img);
        } else {
            // Draw loading indicator for images without thumbnails
            parent_->draw_loading_indicator(item_x, item_y, item_w, item_h);
        }
    }

    fl_pop_clip();

    // Draw progress indicator if generating
    if (parent_->generating_.load()) {
        int completed = parent_->completed_tasks_.load();
        int total = parent_->total_tasks_.load();
        int active = parent_->active_tasks_.load();

        // Draw progress background
        fl_color(FL_YELLOW);
        fl_rectf(x() + w() - 150, y() + 5, 145, 25);
        fl_color(FL_BLACK);
        fl_rect(x() + w() - 150, y() + 5, 145, 25);

        // Draw progress bar
        if (total > 0) {
            int progress_width = (completed * 140) / total;
            fl_color(FL_GREEN);
            fl_rectf(x() + w() - 148, y() + 7, progress_width, 8);
        }

        // Draw progress text
        fl_font(FL_HELVETICA, 9);
        char progress_text[64];
        if (total > 0) {
            snprintf(progress_text, sizeof(progress_text), "%d/%d (%d active)", completed, total, active);
        } else {
            snprintf(progress_text, sizeof(progress_text), "Initializing...");
        }
        fl_color(FL_BLACK);
        fl_draw(progress_text, x() + w() - 145, y() + 23);
    }
}

int Fl_JustifiedLayout_Content::handle(int event) {
    std::cout << "[DEBUG] FLTK UI: Fl_JustifiedLayout_Content::handle() event=" << event << " on thread " << std::this_thread::get_id() << std::endl;
    
    if (!parent_) return 0;

    switch (event) {
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                std::cout << "[DEBUG] FLTK UI: Left mouse click at (" << Fl::event_x() << ", " << Fl::event_y() << ")" << std::endl;
                parent_->handle_click(Fl::event_x(), Fl::event_y());
                return 1;
            }
            break;
    }

    return Fl_Widget::handle(event);
}

void Fl_JustifiedLayout::start_directory_scan(const std::string& dir_path, const std::string& db_path) {
	should_cancel_scan_.store(false);
    if (scanning_.load()) return;

    scanning_.store(true);
    should_cancel_scan_.store(false);

    // Clear current images while scanning
    images_.clear();
    clear_layout();
    clear_image_cache();

    if (content_widget_) {
        content_widget_->redraw();
    }

    // Report start of scanning
    if (progress_callback_) {
        progress_callback_(0, 0, "Starting directory scan...");
    }

    // Start scan thread
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    scan_thread_ = std::thread(&Fl_JustifiedLayout::directory_scan_thread, this, dir_path, db_path);

    std::cout << "Started directory scan for: " << dir_path << std::endl;
}

void Fl_JustifiedLayout::cancel_directory_scan() {
    should_cancel_scan_.store(true);

    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }

    scanning_.store(false);
    std::cout << "Cancelled directory scan" << std::endl;
}

void Fl_JustifiedLayout::directory_scan_thread(const std::string& dir_path, const std::string& db_path) {
    std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " started for: " << dir_path
              << " using database: " << db_path << std::endl;

    try {
        // Create/open database
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " opening database" << std::endl;
        auto scan_database = std::make_unique<DatabaseManager>();
        if (!scan_database->open(db_path)) {
            std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " failed to open database" << std::endl;
            debug_awake([](void* data) {
                std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " directory scan database open failure callback" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Failed to open database");
                }
            }, this);
            scanning_.store(false);
            return;
        }
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " database opened successfully" << std::endl;

        // Set up callback for immediate layout population as metadata becomes available
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " setting up image info callback" << std::endl;
        scan_database->set_image_info_callback([this](const ImageInfo& info) {
            // This callback is called from worker threads, so we need to use Fl::awake
            std::cout << "[DEBUG] Worker thread " << std::this_thread::get_id() << " image info callback triggered for: "
                      << info.path << " (hash: " << info.hash << ")" << std::endl;
            auto info_copy = std::make_shared<ImageInfo>(info);
            debug_awake([](void* data) {
                std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " handling image info ready callback" << std::endl;
                auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>*>(data);
                params->first->handle_image_info_ready(*(params->second));
                delete params;
            }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>(this, info_copy), "image info ready from scan");
        });

        // Create timer and reporter for the scan
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " creating timer and reporter" << std::endl;
        Timer timer;
        StatusReporter reporter(1); // Report every second
        reporter.start();

        // Forward initial progress
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " sending initial progress update" << std::endl;
        debug_awake([](void* data) {
            std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " initial scan progress callback" << std::endl;
            Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
            if (widget->progress_callback_) {
                widget->progress_callback_(0, 0, "Starting directory scan...");
            }
        }, this);

        // Track explicit user cancellation
        std::atomic<bool> user_cancelled(false);
        std::atomic<size_t> last_image_count(0);

        // Start a cancellation monitor thread that also provides incremental updates
        std::cout << "[DEBUG] Directory scan thread " << std::this_thread::get_id() << " starting update monitor thread" << std::endl;
        std::thread update_monitor([this, scan_database = scan_database.get(), &user_cancelled, &last_image_count]() {
            std::cout << "[DEBUG] Update monitor thread " << std::this_thread::get_id() << " started" << std::endl;
            int update_count = 0;
            while (scanning_.load()) {
                update_count++;
                if (should_cancel_scan_.load()) {
                    std::cout << "[DEBUG] Update monitor thread " << std::this_thread::get_id() << " user cancellation requested" << std::endl;
                    user_cancelled.store(true);
                    scan_database->cancel_scan();
                    break;
                }

                // Check for new images and provide incremental updates
                try {
                    std::vector<ImageInfo> new_images = scan_database->get_images_since_count(last_image_count.load());
                    if (!new_images.empty()) {
                        std::cout << "[DEBUG] Update monitor thread " << std::this_thread::get_id() << " found "
                                  << new_images.size() << " new images (update #" << update_count << ")" << std::endl;
                        last_image_count.store(last_image_count.load() + new_images.size());

                        // Create a copy for the lambda to capture
                        auto new_images_copy = std::make_shared<std::vector<ImageInfo>>(std::move(new_images));

                        debug_awake([](void* data) {
                            std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " adding images incrementally" << std::endl;
                            auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>*>(data);
                            params->first->add_images_incremental(*(params->second));
                            delete params;
                        }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>(this, new_images_copy), "incremental image batch");
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error during incremental update: " << e.what() << std::endl;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(250)); // Check 4 times per second
            }
        });

        // Perform the actual directory scan
        int processed = scan_database->scan_directory_parallel(dir_path, timer, reporter);

	// Done with scanning
	scanning_.store(false);

	// Wait for update monitor
	update_monitor.join();

	// We can stop reporting now
        reporter.stop();

        std::cout << "[DEBUG] should_cancel_scan_: " << should_cancel_scan_.load()
                  << ", user_cancelled: " << user_cancelled.load()
                  << ", processed: " << processed << std::endl;

        if (user_cancelled.load()) {
            scan_database->cancel_scan(); // Ensure cancellation is signaled
	    std::cout << "[DEBUG] About to call Fl::awake for scan cancelled, thread id: " << std::this_thread::get_id() << std::endl;
            debug_awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (cancelled) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan cancelled");
                }
            }, this, "scan cancelled");
        } else if (processed >= 0) {
            // Scan completed successfully - get any remaining images and finish
            std::vector<ImageInfo> remaining_images = scan_database->get_images_since_count(last_image_count.load());
            if (!remaining_images.empty()) {
                auto remaining_images_copy = std::make_shared<std::vector<ImageInfo>>(std::move(remaining_images));
                debug_awake([](void* data) {
                    auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>*>(data);
                    params->first->add_images_incremental(*(params->second));
                    delete params;
                }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>(this, remaining_images_copy));
            }

            // Update database and complete
            current_db_path_ = db_path;
            database_ = std::move(scan_database);

	    std::cout << "[DEBUG] About to call Fl::awake for scan complete, thread id: " << std::this_thread::get_id() << std::endl;
            debug_awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (scan complete) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan complete - " + std::to_string(widget->images_.size()) + " images loaded");
                }
                // Start background thumbnail generation
                widget->start_background_generation();
            }, this, "scan complete");
        } else {
	    std::cout << "[DEBUG] About to call Fl::awake for scan failed, thread id: " << std::this_thread::get_id() << std::endl;
            debug_awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (scan failed) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan failed");
                }
            }, this, "scan failed");
        }

        std::cout << "[DEBUG] directory_scan_thread (end of try) scanning_ = " << scanning_.load() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[DEBUG] Exception caught in directory_scan_thread: " << e.what() << std::endl;
        debug_awake([](void* data) {
            auto* params = static_cast<std::pair<Fl_JustifiedLayout*, std::string>*>(data);
            std::cout << "[DEBUG] Fl::awake (exception) lambda running" << std::endl;
            if (params->first->progress_callback_) {
                params->first->progress_callback_(0, 0, "Scan error: " + params->second);
            }
            delete params;
        }, new std::pair<Fl_JustifiedLayout*, std::string>(this, e.what()), "scan exception");
    }

}

void Fl_JustifiedLayout::complete_directory_scan() {
    std::cout << "[DEBUG] complete_directory_scan() called in GUI thread" << std::endl;
    if (progress_callback_) {
        progress_callback_(0, 0, "Loading images from database...");
    }

    // Reload images from the updated database
    if (load_image_list()) {
        if (progress_callback_) {
            progress_callback_(0, 0, "Scan complete - " + std::to_string(images_.size()) + " images loaded");
        }

        // Start background thumbnail generation
        start_background_generation();

        std::cout << "Directory scan completed successfully - loaded " << images_.size() << " images" << std::endl;
    } else {
        if (progress_callback_) {
            progress_callback_(0, 0, "Failed to load images from database");
        }
    }
}

bool Fl_JustifiedLayout::load_image_list() {
    std::cout << "[DEBUG] load_image_list() called" << std::endl;
    images_.clear();
    clear_image_cache();  // Clear cached images when loading new list

    if (!database_) {
        std::cout << "No database available, creating mock data for testing" << std::endl;

        // ... [mock image code unchanged] ...
        std::cout << "Loaded " << images_.size() << " mock images" << std::endl;
    } else {
        // Load real images from database
        try {
            images_ = database_->get_all_images();
            std::cout << "Loaded " << images_.size() << " images from database" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error loading images from database: " << e.what() << std::endl;
            return false;
        }
    }

    // Trigger layout calculation
    relayout();

    return true;
}

void Fl_JustifiedLayout::add_images_incremental(const std::vector<ImageInfo>& new_images) {
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " add_images_incremental called with "
              << new_images.size() << " new images" << std::endl;

    if (new_images.empty()) {
        std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " no new images to add, returning" << std::endl;
        return;
    }

    // Add new images to our list
    size_t old_size = images_.size();
    images_.insert(images_.end(), new_images.begin(), new_images.end());

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " added " << new_images.size()
              << " images, total count increased from " << old_size << " to " << images_.size() << std::endl;

    // For incremental layout, we need to recalculate layout with all images
    // This is more efficient than a full clear since we preserve existing layout cache
    // when possible, but for now we do a simple approach
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " recalculating layout for incremental add" << std::endl;
    calculate_layout();

    // Resize content widget to match the new total layout height
    if (content_widget_) {
        int content_height = std::max(static_cast<int>(total_height_), h());
        std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " resizing content widget for incremental add, new height: "
                  << content_height << std::endl;
        content_widget_->resize(x(), y(), w(), content_height);
    } else {
        std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " no content widget to resize for incremental add" << std::endl;
    }

    // Trigger redraw to show new placeholders
    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " triggering redraw for incremental add" << std::endl;
    redraw();

    std::cout << "[DEBUG] UI thread " << std::this_thread::get_id() << " Added " << new_images.size()
              << " images incrementally (total: " << images_.size() << ")" << std::endl;
}

// Batch processing and debug logging methods
void Fl_JustifiedLayout::log_batch_debug(const std::string& message) const {
    if (!batch_config_.enable_debug_logging) return;
    
    std::lock_guard<std::mutex> lock(debug_log_mutex_);
    std::cout << "[BATCH] UI thread " << std::this_thread::get_id() << " " << message << std::endl;
}

void Fl_JustifiedLayout::log_ui_debug(const std::string& message) const {
    if (!batch_config_.enable_debug_logging) return;
    
    std::lock_guard<std::mutex> lock(debug_log_mutex_);
    std::cout << "[UI] Thread " << std::this_thread::get_id() << " " << message << std::endl;
}

void Fl_JustifiedLayout::queue_image_info_batch(const ImageInfo& info) {
    log_batch_debug("queue_image_info_batch called for: " + info.path + " (hash: " + info.hash + ")");
    
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    current_batch_.pending_images.push_back(info);
    current_batch_.total_images_added++;
    
    size_t pending_count = current_batch_.pending_images.size();
    log_batch_debug("Added image to batch, pending count: " + std::to_string(pending_count) + 
                   ", total added: " + std::to_string(current_batch_.total_images_added));
    
    // Check if we should process immediately (small batch) or schedule for later
    if (pending_count <= batch_config_.small_batch_threshold) {
        log_batch_debug("Small batch detected (" + std::to_string(pending_count) + 
                       " <= " + std::to_string(batch_config_.small_batch_threshold) + 
                       "), processing immediately for snappy UI feedback");
        
        // Process immediately for small batches to maintain snappy UI
        std::vector<ImageInfo> immediate_batch = std::move(current_batch_.pending_images);
        current_batch_.pending_images.clear();
        current_batch_.last_batch_time = std::chrono::steady_clock::now();
        
        // Release lock before processing
        lock.~lock_guard();
        process_image_info_batch(immediate_batch);
    } else {
        // For larger batches, schedule a flush if not already scheduled
        if (!batch_flush_scheduled_.load()) {
            log_batch_debug("Large batch detected (" + std::to_string(pending_count) + 
                           " > " + std::to_string(batch_config_.small_batch_threshold) + 
                           "), scheduling batch flush");
            
            batch_flush_scheduled_.store(true);
            Fl::add_timeout(batch_config_.batch_timeout_ms / 1000.0, batch_flush_callback, this);
        }
    }
}

void Fl_JustifiedLayout::flush_pending_image_batch(bool force) {
    std::lock_guard<std::mutex> lock(batch_mutex_);
    
    if (current_batch_.pending_images.empty()) {
        log_batch_debug("Flush called but no pending images");
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(now - current_batch_.last_batch_time).count();
    
    size_t pending_count = current_batch_.pending_images.size();
    
    if (force || pending_count >= batch_config_.large_batch_size || elapsed_ms >= batch_config_.batch_timeout_ms) {
        log_batch_debug("Flushing batch: force=" + std::string(force ? "true" : "false") + 
                       ", pending=" + std::to_string(pending_count) + 
                       ", elapsed=" + std::to_string(elapsed_ms) + "ms");
        
        std::vector<ImageInfo> batch_to_process = std::move(current_batch_.pending_images);
        current_batch_.pending_images.clear();
        current_batch_.last_batch_time = now;
        current_batch_.total_batches_processed++;
        
        // Release lock before processing
        lock.~lock_guard();
        process_image_info_batch(batch_to_process);
    } else {
        log_batch_debug("Flush conditions not met: pending=" + std::to_string(pending_count) + 
                       ", elapsed=" + std::to_string(elapsed_ms) + "ms, will wait");
    }
}

void Fl_JustifiedLayout::process_image_info_batch(const std::vector<ImageInfo>& batch) {
    if (batch.empty()) {
        log_batch_debug("process_image_info_batch called with empty batch");
        return;
    }
    
    log_batch_debug("Processing batch of " + std::to_string(batch.size()) + " images");
    
    // Add images to our list in batch
    size_t old_size = images_.size();
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex_);
        
        for (const auto& info : batch) {
            images_.push_back(info);
            hash_to_index_map_[info.hash] = images_.size() - 1;
            
            log_batch_debug("Added image to main list: " + info.path + 
                           " (index: " + std::to_string(images_.size() - 1) + 
                           ", has_thumbnails: " + (info.has_thumbnails ? "true" : "false") + ")");
        }
    }
    
    log_batch_debug("Batch processed: added " + std::to_string(batch.size()) + 
                   " images, total count increased from " + std::to_string(old_size) + 
                   " to " + std::to_string(images_.size()));
    
    // Recalculate layout for the batch
    log_ui_debug("Recalculating layout for batch of " + std::to_string(batch.size()) + " images");
    calculate_layout();
    
    // Resize content widget to match the new total layout height
    if (content_widget_) {
        int content_height = std::max(static_cast<int>(total_height_), h());
        log_ui_debug("Resizing content widget for batch, new height: " + std::to_string(content_height));
        content_widget_->resize(x(), y(), w(), content_height);
    }
    
    // Trigger redraw to show new placeholders
    log_ui_debug("Triggering redraw for batch of " + std::to_string(batch.size()) + " images");
    redraw();
    
    log_batch_debug("Completed processing batch of " + std::to_string(batch.size()) + " images");
}

// Static callback for batch processing
void Fl_JustifiedLayout::batch_flush_callback(void* data) {
    if (!data) return;
    
    Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
    widget->log_batch_debug("Batch flush callback triggered by timeout");
    
    widget->batch_flush_scheduled_.store(false);
    widget->flush_pending_image_batch(false);
}

// Two-stage population support methods
void Fl_JustifiedLayout::handle_image_info_ready(const ImageInfo& info) {
    log_ui_debug("handle_image_info_ready called for: " + info.path + 
                " (hash: " + info.hash + ", has_thumbnails: " + 
                (info.has_thumbnails ? "true" : "false") + ")");
    
    // Instead of processing immediately, queue for batch processing
    queue_image_info_batch(info);
}

void Fl_JustifiedLayout::handle_thumbnail_ready(const std::string& hash) {
    std::cout << "[DEBUG] Thumbnail worker thread " << std::this_thread::get_id() << " handle_thumbnail_ready called for hash: " << hash << std::endl;

    // This is called when a thumbnail becomes available (stage 2)
    ThumbnailNotification notification(hash, true);
    thumbnail_notifications_.enqueue(notification);

    std::cout << "[DEBUG] Thumbnail worker thread " << std::this_thread::get_id() << " enqueued thumbnail notification and scheduling UI awake for hash: " << hash << std::endl;

    // Schedule UI update
    debug_awake(thumbnail_notification_callback, this, "thumbnail ready notification");
}

void Fl_JustifiedLayout::process_thumbnail_notifications() {
    ThumbnailNotification notification("", false);
    bool needs_redraw = false;

    while (thumbnail_notifications_.try_dequeue(notification)) {
        if (notification.is_ready) {
            // Find the image by hash and update it
            auto it = hash_to_index_map_.find(notification.hash);
            if (it != hash_to_index_map_.end()) {
                int index = it->second;
                if (index >= 0 && index < static_cast<int>(images_.size())) {
                    std::lock_guard<std::mutex> lock(image_cache_mutex_);

                    // Reload image info from database to get thumbnail data
                    if (database_) {
                        MDB_txn* read_txn;
                        if (database_->begin_read_transaction(read_txn)) {
                            if (database_->load_image_info(read_txn, notification.hash, images_[index])) {
                                // Clear cached image so it will be reloaded with thumbnail
                                auto cache_it = image_cache_.find(notification.hash);
                                if (cache_it != image_cache_.end()) {
                                    image_cache_.erase(cache_it);
                                }
                                needs_redraw = true;
                            }
                            database_->abort_transaction(read_txn);
                        }
                    }
                }
            }
        }
    }

    if (needs_redraw) {
        redraw();
    }
}

void Fl_JustifiedLayout::draw_loading_indicator(int x, int y, int w, int h) {
    // Draw a simple loading placeholder
    fl_color(FL_LIGHT2);
    fl_rectf(x, y, w, h);

    // Draw border
    fl_color(FL_DARK3);
    fl_rect(x, y, w, h);

    // Draw loading text or spinner
    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA, 12);

    const char* text = "Loading...";
    int text_width = static_cast<int>(fl_width(text));
    int text_height = fl_height();

    int text_x = x + (w - text_width) / 2;
    int text_y = y + (h + text_height) / 2 - fl_descent();

    fl_draw(text, text_x, text_y);
}

// Static callback for thumbnail notifications
void Fl_JustifiedLayout::thumbnail_notification_callback(void* data) {
    if (data) {
        static_cast<Fl_JustifiedLayout*>(data)->process_thumbnail_notifications();
    }
}

void Fl_JustifiedLayout::update_visibility_and_queue_thumbnails() {
    if (layout_items_.empty() || images_.empty()) {
        return;
    }

    // Get current scroll position
    int scroll_y = yposition();
    int viewport_height = h();

    // Define visible area with some padding for preloading
    int visible_start = scroll_y - viewport_height;  // Preload above viewport
    int visible_end = scroll_y + 2 * viewport_height;  // Preload below viewport

    std::vector<int> high_priority_indices;
    std::vector<int> low_priority_indices;

    for (size_t i = 0; i < layout_items_.size() && i < images_.size(); i++) {
        const auto& item = layout_items_[i];
        const auto& img = images_[i];

        // Skip images that already have thumbnails
        if (img.has_thumbnails) {
            continue;
        }

        int item_y = static_cast<int>(item.t);
        int item_bottom = item_y + static_cast<int>(item.h);

        // Check if item intersects with visible area
        if (item_bottom >= visible_start && item_y <= visible_end) {
            // Check if actually visible in current viewport
            if (item_bottom >= scroll_y && item_y <= scroll_y + viewport_height) {
                high_priority_indices.push_back(static_cast<int>(i));
            } else {
                // Near visible area, lower priority
                low_priority_indices.push_back(static_cast<int>(i));
            }
        }
    }

    // Queue high priority thumbnails first
    if (!high_priority_indices.empty()) {
        int queued = queue_thumbnail_tasks(high_priority_indices, ThumbnailPriority::HIGH);
        std::cout << "Queued " << queued << " high-priority thumbnail tasks" << std::endl;
    }

    // Queue lower priority thumbnails
    if (!low_priority_indices.empty()) {
        int queued = queue_thumbnail_tasks(low_priority_indices, ThumbnailPriority::LOW);
        std::cout << "Queued " << queued << " low-priority thumbnail tasks" << std::endl;
    }
}

void Fl_JustifiedLayout::write_debug_output() {
    if (!debug_output_enabled_ || layout_items_.empty()) {
        return;
    }

    // Ensure output directory exists
    std::filesystem::create_directories(debug_output_dir_);

    // Generate filename with counter
    std::string filename = debug_output_dir_ + "/layout_update_" +
                          std::to_string(debug_update_counter_++) + "." + debug_output_format_;

    if (debug_output_format_ == "svg") {
        write_debug_svg(filename);
    } else if (debug_output_format_ == "png") {
        write_debug_png(filename);
    }

    std::cout << "Debug layout output written to: " << filename << " (images: " << images_.size() << ")" << std::endl;
}

void Fl_JustifiedLayout::write_debug_svg(const std::string& filename) {
    // Use the simple-svg library that's already included
    svg::Document doc(filename, svg::Layout(svg::Dimensions(layout_config_.w, total_height_), svg::Layout::BottomLeft));

    // Draw background
    doc << svg::Rectangle(svg::Point(0, 0), layout_config_.w, total_height_, svg::Fill(svg::Color(255, 255, 255)));

    // Draw each item
    for (size_t i = 0; i < layout_items_.size() && i < images_.size(); ++i) {
        const auto& item = layout_items_[i];
        const auto& info = images_[i];

        // Choose color based on thumbnail availability
        svg::Color fill_color = info.has_thumbnails ? svg::Color(0, 255, 0) : svg::Color(255, 255, 0);  // Green : Yellow
        svg::Color stroke_color = svg::Color(0, 0, 0);  // Black

        // Draw rectangle for this image
        doc << svg::Rectangle(svg::Point(item.l, item.t), item.w, item.h,
                             svg::Fill(fill_color), svg::Stroke(1, stroke_color));

        // Add text with image info
        if (item.w > 100 && item.h > 30) {  // Only add text if rectangle is large enough
            std::string text = std::to_string(static_cast<int>(item.w)) + "x" +
                              std::to_string(static_cast<int>(item.h));
            doc << svg::Text(svg::Point(item.l + 10, item.t + 20), text,
                           svg::Fill(svg::Color(0, 0, 0)), svg::Font(12, "Arial"));
        }
    }

    doc.save();
}

void Fl_JustifiedLayout::write_debug_png(const std::string& filename) {
    // For PNG output, we'll create a simple image using FLTK's image surface
    // This is a placeholder implementation - for now we'll just create a simple colored rectangle
    std::cout << "PNG debug output not yet fully implemented, using SVG fallback" << std::endl;

    // Fall back to SVG for now
    std::string svg_filename = filename;
    size_t dot_pos = svg_filename.find_last_of('.');
    if (dot_pos != std::string::npos) {
        svg_filename = svg_filename.substr(0, dot_pos) + ".svg";
    }
    write_debug_svg(svg_filename);
}

void Fl_JustifiedLayout::draw() {
    std::cout << "[DEBUG] FLTK UI: Fl_JustifiedLayout::draw() called on thread " << std::this_thread::get_id() 
              << " (images: " << images_.size() << ", visible: " << visible_start_idx_ << "-" << visible_end_idx_ << ")" << std::endl;
    
    // Fl_Scroll handles its own drawing and scrollbar management
    // The content widget (Fl_JustifiedLayout_Content) handles the actual thumbnail drawing
    Fl_Scroll::draw();
    
    std::cout << "[DEBUG] FLTK UI: Fl_JustifiedLayout::draw() completed" << std::endl;
}

