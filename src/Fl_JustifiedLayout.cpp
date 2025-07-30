/*
 * Fl_JustifiedLayout.cpp - FLTK widget for displaying thumbnails in justified layout
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "Fl_JustifiedLayout.hpp"
#include "thread_manager.hpp"
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
#include "utils.hpp"

// Helper function to wrap Fl::awake call
inline void debug_awake(void (*callback)(void*), void* data, const std::string& description = "") {
    Fl::awake(callback, data);
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
    , scanning_(false)
    , should_cancel_scan_(false)
    , thread_manager_(nullptr)
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

void Fl_JustifiedLayout::result_processor_callback(void* data) {
    if (data) {
	static_cast<Fl_JustifiedLayout*>(data)->process_thread_manager_results();
    }
}

void Fl_JustifiedLayout::progress_update_callback(void* data) {
    if (data) {
	Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
	if (widget->content_widget_) {
	    widget->content_widget_->redraw();
	}
    }
}


void Fl_JustifiedLayout::process_thread_manager_results() {
    if (!thread_manager_) {
	return; // No ThreadManager available
    }

    static std::atomic_flag processing = ATOMIC_FLAG_INIT;
    if (processing.test_and_set()) {
	return;  // Avoid re-entry
    }

    UIDrawTask result;
    bool any_processed = false;
    int results_processed = 0;

    // Process all available results from ThreadManager
    while (thread_manager_->get_thumbnail_result(result)) {
	results_processed++;

	if (result.thumbnail) {
	    std::lock_guard<std::mutex> lock(image_cache_mutex_);
	    if (image_cache_.find(result.cache_key) != image_cache_.end()) {
		continue; // Already in cache, skip duplicate job
	    }
	    image_cache_[result.cache_key] = std::move(result.thumbnail);
	    any_processed = true;

	    log_ui_debug("Processed ThreadManager result for image " + std::to_string(result.image_index) +
		" (cache_key: " + result.cache_key + ")");
	}
    }

    log_batch_debug("Processed " + std::to_string(results_processed) + " ThreadManager thumbnail results, any_processed: " +
	    (any_processed ? "true" : "false"));

    // Trigger redraw if any thumbnails were processed
    if (any_processed) {
	if (content_widget_) {
	    content_widget_->redraw();
	}
    }

    processing.clear();
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
    clear_image_cache();
}

bool Fl_JustifiedLayout::set_database_path(const std::string& db_path) {
    current_db_path_ = db_path;

    // Create and initialize database manager
    database_ = std::make_unique<DatabaseManager>();

    // Set up callback for two-stage processing
    database_->set_image_info_callback([this](const ImageInfo& info) {
	    // This callback is called from worker threads, so we need to use Fl::awake
	    auto info_copy = std::make_shared<ImageInfo>(info);
	    debug_awake([](void* data) {
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

    // Use common cache database path instead of directory-specific path
    std::string db_path = get_cache_db_path();

    // Start asynchronous directory scan
    start_directory_scan(dir_path, db_path);

    return true;
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
	case FL_PUSH:
	    break;
	case FL_RELEASE:
	    break;
	case FL_KEYDOWN:
	case FL_KEYUP:
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

    // Queue visible region for high priority if ThreadManager is available
    if (thread_manager_) {
	update_visibility_and_queue_thumbnails();
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

    // Trigger prefetching when visible region is calculated through ThreadManager
    if (thread_manager_) {
	update_visibility_and_queue_thumbnails();
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
    std::string cache_key = make_thumbnail_key(info.hash, target_width, target_height);

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
    // Check if ThreadManager is available for thumbnail generation
    bool is_loading = (thread_manager_ != nullptr);

    // Draw outer border with different color based on state
    if (is_loading) {
	fl_color(FL_BLUE);  // Blue border when ThreadManager is available
    } else {
	fl_color(FL_DARK2); // Darker border when no thumbnail system available
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

    // Process any available thumbnail results from ThreadManager
    parent_->process_thread_manager_results();

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
}

void Fl_JustifiedLayout::cancel_directory_scan() {
    should_cancel_scan_.store(true);

    if (scan_thread_.joinable()) {
	scan_thread_.join();
    }

    scanning_.store(false);
}

void Fl_JustifiedLayout::directory_scan_thread(const std::string& dir_path, const std::string& db_path) {

    try {
	// Create/open database
	auto scan_database = std::make_unique<DatabaseManager>();
	if (!scan_database->open(db_path)) {
	    debug_awake([](void* data) {
		    Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
		    if (widget->progress_callback_) {
		    widget->progress_callback_(0, 0, "Failed to open database");
		    }
		    }, this);
	    scanning_.store(false);
	    return;
	}

	// Set up callback for immediate layout population as metadata becomes available
	scan_database->set_image_info_callback([this](const ImageInfo& info) {
		// This callback is called from worker threads, so we need to use Fl::awake
		auto info_copy = std::make_shared<ImageInfo>(info);
		debug_awake([](void* data) {
			auto params = static_cast<std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>*>(data);
			params->first->handle_image_info_ready(*(params->second));
			delete params;
			}, new std::pair<Fl_JustifiedLayout*, std::shared_ptr<ImageInfo>>(this, info_copy), "image info ready from scan");
		});

	// Create timer and reporter for the scan
	Timer timer;
	StatusReporter reporter(1); // Report every second
	reporter.start();

	// Forward initial progress
	debug_awake([](void* data) {
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
		int update_count = 0;
		while (scanning_.load()) {
		update_count++;
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

		debug_awake([](void* data) {
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


	if (user_cancelled.load()) {
	    scan_database->cancel_scan(); // Ensure cancellation is signaled
	    debug_awake([](void* data) {
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

	    debug_awake([](void* data) {
		    Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
		    if (widget->progress_callback_) {
		    widget->progress_callback_(0, 0, "Scan complete - " + std::to_string(widget->images_.size()) + " images loaded");
		    }
		    }, this, "scan complete");
	} else {
	    debug_awake([](void* data) {
		    Fl_JustifiedLayout* widget = static_cast<Fl_JustifiedLayout*>(data);
		    if (widget->progress_callback_) {
		    widget->progress_callback_(0, 0, "Scan failed");
		    }
		    }, this, "scan failed");
	}

    } catch (const std::exception& e) {
	debug_awake([](void* data) {
		auto* params = static_cast<std::pair<Fl_JustifiedLayout*, std::string>*>(data);
		if (params->first->progress_callback_) {
		params->first->progress_callback_(0, 0, "Scan error: " + params->second);
		}
		delete params;
		}, new std::pair<Fl_JustifiedLayout*, std::string>(this, e.what()), "scan exception");
    }

}

void Fl_JustifiedLayout::complete_directory_scan() {
    if (progress_callback_) {
	progress_callback_(0, 0, "Loading images from database...");
    }

    // Reload images from the updated database
    if (load_image_list()) {
	if (progress_callback_) {
	    progress_callback_(0, 0, "Scan complete - " + std::to_string(images_.size()) + " images loaded");
	}
    } else {
	if (progress_callback_) {
	    progress_callback_(0, 0, "Failed to load images from database");
	}
    }
}

bool Fl_JustifiedLayout::load_image_list() {
    images_.clear();
    clear_image_cache();  // Clear cached images when loading new list

    if (!database_) {
	// ... [mock image code unchanged] ...
    } else {
	// Load real images from database
	try {
	    images_ = database_->get_all_images();
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

    if (new_images.empty()) {
	return;
    }

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
    } else {
    }

    // Trigger redraw to show new placeholders
    redraw();

}

// Batch processing and debug logging methods
void Fl_JustifiedLayout::log_batch_debug(const std::string& message) const {
    // Debug logging disabled in production
}

void Fl_JustifiedLayout::log_ui_debug(const std::string& message) const {
    // Debug logging disabled in production
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

    // Queue thumbnail requests for new images if ThreadManager is available
    if (thread_manager_) {
	log_ui_debug("ThreadManager available, queuing thumbnail requests for " + std::to_string(batch.size()) + " new images");
	for (size_t i = 0; i < batch.size(); ++i) {
	    const auto& info = batch[i];
	    int image_index = old_size + i; // Image index in the main images_ vector

	    // Create UIThumbnailTask for this image
	    UIThumbnailTask task(
		image_index,
		UIThumbnailTask::HIGH, // High priority for newly added images
		static_cast<int>(layout_config_.rh * info.aspect_ratio), // target_width
		static_cast<int>(layout_config_.rh), // target_height
		info.hash
	    );

	    log_ui_debug("Queuing UIThumbnailTask for image " + std::to_string(image_index) +
		" (hash: " + make_thumbnail_key(info.hash, task.target_width, task.target_height) + ")");

	    thread_manager_->request_thumbnail(task);
	}
    } else {
	log_ui_debug("ThreadManager not available, using legacy thumbnail system");
    }

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

    // This is called when a thumbnail becomes available (stage 2)
    ThumbnailNotification notification(hash, true);
    thumbnail_notifications_.enqueue(notification);


    // Schedule UI update
    debug_awake(thumbnail_notification_callback, this, "thumbnail ready notification");
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
	static_cast<Fl_JustifiedLayout*>(data)->redraw();
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
    if (!high_priority_indices.empty() && thread_manager_) {
	// Use ThreadManager for thumbnail requests
	for (int idx : high_priority_indices) {
	    if (idx >= 0 && idx < static_cast<int>(images_.size()) && idx < static_cast<int>(layout_items_.size())) {
		const auto& item = layout_items_[idx];
		const auto& img = images_[idx];

		UIThumbnailTask task(
		    idx,
		    UIThumbnailTask::HIGH,
		    static_cast<int>(item.w) - 2, // Account for border
		    static_cast<int>(item.h) - 2, // Account for border
		    img.hash
		);

		thread_manager_->request_thumbnail(task);
	    }
	}
    }

    // Queue lower priority thumbnails
    if (!low_priority_indices.empty() && thread_manager_) {
	// Use ThreadManager for thumbnail requests
	for (int idx : low_priority_indices) {
	    if (idx >= 0 && idx < static_cast<int>(images_.size()) && idx < static_cast<int>(layout_items_.size())) {
		const auto& item = layout_items_[idx];
		const auto& img = images_[idx];

		UIThumbnailTask task(
		    idx,
		    UIThumbnailTask::LOW,
		    static_cast<int>(item.w) - 2, // Account for border
		    static_cast<int>(item.h) - 2, // Account for border
		    img.hash
		);

		thread_manager_->request_thumbnail(task);
	    }
	}
    }
}

void Fl_JustifiedLayout::draw() {

    // Fl_Scroll handles its own drawing and scrollbar management
    // The content widget (Fl_JustifiedLayout_Content) handles the actual thumbnail drawing
    Fl_Scroll::draw();

}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
