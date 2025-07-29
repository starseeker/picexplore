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
#include "stb_image.h"
#include "../utils.h"

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
    ThumbnailTask task;

    while (!should_stop_.load()) {
        bool found_task = false;

        // Try high priority queue first
        if (high_priority_queue_.try_dequeue(task)) {
            found_task = true;
        }
        // Fall back to low priority queue
        else if (low_priority_queue_.try_dequeue(task)) {
            found_task = true;
        }

        if (found_task) {
            active_tasks_.fetch_add(1);

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
                    ThumbnailResult result(task.image_index, std::move(thumbnail), cache_key);
                    result_queue_.enqueue(std::move(result));
                    Fl::awake(result_processor_callback, this);
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
        std::cerr << "WARNING: process_thumbnail_results re-entered!" << std::endl;
        return;
    }
    ThumbnailResult result;
    bool any_processed = false;

    // Process all available results
    while (result_queue_.try_dequeue(result)) {
        if (result.thumbnail) {
            std::lock_guard<std::mutex> lock(image_cache_mutex_);
	    if (image_cache_.find(result.cache_key) != image_cache_.end()) {
	       continue; // Already in cache, skip duplicate job
	    }
            image_cache_[result.cache_key] = std::move(result.thumbnail);
            any_processed = true;
        } else {
		std::cerr << "WARNING: Dequeued null thumbnail for key=" << result.cache_key << std::endl;
	}
    }

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
    return num_queued;
}

Fl_JustifiedLayout::~Fl_JustifiedLayout() {
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
        this->handle_image_info_ready(info);
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

    worker_threads_.clear();
    worker_threads_.reserve(num_workers);

    for (int i = 0; i < num_workers; ++i) {
        worker_threads_.emplace_back(&Fl_JustifiedLayout::thumbnail_worker_thread, this);
    }

    std::cout << "Started background thumbnail generation with " << num_workers << " workers" << std::endl;

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
    should_stop_.store(true);

    // Wait for all worker threads to complete
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();

    // Clear queues
    ThumbnailTask task;
    while (high_priority_queue_.try_dequeue(task)) {}
    while (low_priority_queue_.try_dequeue(task)) {}

    ThumbnailResult result;
    while (result_queue_.try_dequeue(result)) {}

    generating_.store(false);

    // Remove any pending progress update timers
    Fl::remove_timeout(progress_update_callback, this);

    std::cout << "Stopped background thumbnail generation" << std::endl;
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
    switch (event) {
        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;
        case FL_MOVE:
        case FL_DRAG:
            // Check for scroll position changes
            update_visibility_and_queue_thumbnails();
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
    if (!parent_) return;

    // Clear background
    fl_color(parent_->color());
    fl_rectf(x(), y(), w(), h());

    if (parent_->images_.empty()) {
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
    if (!parent_) return 0;

    switch (event) {
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE) {
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
    try {
        // Create/open database
        auto scan_database = std::make_unique<DatabaseManager>();
        if (!scan_database->open(db_path)) {
            Fl::awake([](void* data) {
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Failed to open database");
                }
            }, this);
            scanning_.store(false);
            return;
        }

        // Create timer and reporter for the scan
        Timer timer;
        StatusReporter reporter(1); // Report every second
        reporter.start();

        // Forward initial progress
        Fl::awake([](void* data) {
            Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
            if (widget->progress_callback_) {
                widget->progress_callback_(0, 0, "Starting directory scan...");
            }
        }, this);

        // Track explicit user cancellation
        std::atomic<bool> user_cancelled(false);
        std::atomic<size_t> last_image_count(0);

        // Start a cancellation monitor thread that also provides incremental updates
        std::thread update_monitor([this, scan_database = scan_database.get(), &user_cancelled, &last_image_count]() {
            while (scanning_.load()) {
                if (should_cancel_scan_.load()) {
                    user_cancelled.store(true);
                    scan_database->cancel_scan();
                    break;
                }

                // Check for new images and provide incremental updates
                try {
                    std::vector<ImageInfo> new_images = scan_database->get_images_since_count(last_image_count.load());
                    if (!new_images.empty()) {
                        last_image_count.store(last_image_count.load() + new_images.size());

                        // Create a copy for the lambda to capture
                        auto new_images_copy = std::make_shared<std::vector<ImageInfo>>(std::move(new_images));

                        Fl::awake([](void* data) {
                            auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>*>(data);
                            params->first->add_images_incremental(*(params->second));
                            delete params;
                        }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>(this, new_images_copy));
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
            Fl::awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (cancelled) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan cancelled");
                }
            }, this);
        } else if (processed >= 0) {
            // Scan completed successfully - get any remaining images and finish
            std::vector<ImageInfo> remaining_images = scan_database->get_images_since_count(last_image_count.load());
            if (!remaining_images.empty()) {
                auto remaining_images_copy = std::make_shared<std::vector<ImageInfo>>(std::move(remaining_images));
                Fl::awake([](void* data) {
                    auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>*>(data);
                    params->first->add_images_incremental(*(params->second));
                    delete params;
                }, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<std::vector<ImageInfo>>>(this, remaining_images_copy));
            }

            // Update database and complete
            current_db_path_ = db_path;
            database_ = std::move(scan_database);

	    std::cout << "[DEBUG] About to call Fl::awake for scan complete, thread id: " << std::this_thread::get_id() << std::endl;
            Fl::awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (scan complete) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan complete - " + std::to_string(widget->images_.size()) + " images loaded");
                }
                // Start background thumbnail generation
                widget->start_background_generation();
            }, this);
        } else {
	    std::cout << "[DEBUG] About to call Fl::awake for scan failed, thread id: " << std::this_thread::get_id() << std::endl;
            Fl::awake([](void* data) {
                std::cout << "[DEBUG] Fl::awake (scan failed) lambda running" << std::endl;
                Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
                if (widget->progress_callback_) {
                    widget->progress_callback_(0, 0, "Scan failed");
                }
            }, this);
        }

        std::cout << "[DEBUG] directory_scan_thread (end of try) scanning_ = " << scanning_.load() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[DEBUG] Exception caught in directory_scan_thread: " << e.what() << std::endl;
        Fl::awake([](void* data) {
            auto* params = static_cast<std::pair<Fl_JustifiedLayout*, std::string>*>(data);
            std::cout << "[DEBUG] Fl::awake (exception) lambda running" << std::endl;
            if (params->first->progress_callback_) {
                params->first->progress_callback_(0, 0, "Scan error: " + params->second);
            }
            delete params;
        }, new std::pair<Fl_JustifiedLayout*, std::string>(this, e.what()));
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
    if (new_images.empty()) return;

    // Add new images to our list
    size_t old_size = images_.size();
    images_.insert(images_.end(), new_images.begin(), new_images.end());

    // For incremental layout, we need to recalculate layout with all images
    // This is more efficient than a full clear since we preserve existing layout cache
    // when possible, but for now we do a simple approach
    calculate_layout();

    // Resize content widget to match the new total layout height
    if (content_widget_) {
        int content_height = std::max(static_cast<int>(total_height_), h());
        content_widget_->resize(x(), y(), w(), content_height);
    }

    // Trigger redraw to show new placeholders
    redraw();

    std::cout << "Added " << new_images.size() << " images incrementally (total: "
              << images_.size() << ")" << std::endl;
}

// Two-stage population support methods
void Fl_JustifiedLayout::handle_image_info_ready(const ImageInfo& info) {
    // This is called from the database scanning thread when metadata is available
    // Add the image to our list immediately (stage 1)
    std::lock_guard<std::mutex> lock(image_cache_mutex_);

    images_.push_back(info);
    hash_to_index_map_[info.hash] = images_.size() - 1;

    // Schedule a layout recalculation and redraw
    Fl::awake(thumbnail_notification_callback, this);
}

void Fl_JustifiedLayout::handle_thumbnail_ready(const std::string& hash) {
    // This is called when a thumbnail becomes available (stage 2)
    ThumbnailNotification notification(hash, true);
    thumbnail_notifications_.enqueue(notification);

    // Schedule UI update
    Fl::awake(thumbnail_notification_callback, this);
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

void Fl_JustifiedLayout::draw() {
    // Fl_Scroll handles its own drawing and scrollbar management
    // The content widget (Fl_JustifiedLayout_Content) handles the actual thumbnail drawing
    Fl_Scroll::draw();
}

