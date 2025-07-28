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
#include "../stb_image.h"

Fl_JustifiedLayout::Fl_JustifiedLayout(int X, int Y, int W, int H, const char* label)
    : Fl_Widget(X, Y, W, H, label)
    , database_(nullptr)
    , layout_config_()
    , total_height_(0)
    , visible_start_idx_(0)
    , visible_end_idx_(0)
    , selected_index_(-1)
    , generating_(false)
    , should_stop_(false)
    , scroll_offset_(0)
{
    // Initialize layout configuration with reasonable defaults
    layout_config_.w = W - 20; // Leave some margin
    layout_config_.rh = DEFAULT_ROW_HEIGHT;
    layout_config_.pt = layout_config_.pr = layout_config_.pb = layout_config_.pl = 10;
    layout_config_.sh = layout_config_.sv = 5;

    // Set widget color scheme
    color(FL_WHITE);
    selection_color(FL_BLUE);
}

Fl_JustifiedLayout::~Fl_JustifiedLayout() {
    stop_background_generation();
    clear_image_cache();
}

bool Fl_JustifiedLayout::set_database_path(const std::string& db_path) {
    current_db_path_ = db_path;

    // Create and initialize database manager
    database_ = std::make_unique<DatabaseManager>();

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
    // For skeleton implementation, just create a default database path
    std::string db_path = dir_path + "/images.db";

    // In full implementation, this would scan the directory and build/update database
    std::cout << "Would scan directory: " << dir_path << " and create/update database: " << db_path << std::endl;

    return set_database_path(db_path);
}

void Fl_JustifiedLayout::start_background_generation() {
    if (generating_.load()) return;

    generating_.store(true);
    should_stop_.store(false);

    // Stub: In full implementation, this would start background threads
    // for thumbnail generation with priority queues
    std::cout << "Started background thumbnail generation (stubbed)" << std::endl;

    if (progress_callback_) {
        progress_callback_(0, images_.size(), "Starting background generation...");
    }
}

void Fl_JustifiedLayout::stop_background_generation() {
    if (!generating_.load()) return;

    should_stop_.store(true);
    generating_.store(false);

    std::cout << "Stopped background thumbnail generation" << std::endl;
}

void Fl_JustifiedLayout::prefetch_visible_region() {
    // Stub: In full implementation, this would queue visible thumbnails for high-priority generation
    std::cout << "Prefetching visible region: " << visible_start_idx_ << " to " << visible_end_idx_ << std::endl;
}

void Fl_JustifiedLayout::prefetch_next_region() {
    // Stub: Calculate and prefetch next page of thumbnails
    int next_start = visible_end_idx_ + 1;
    int next_end = std::min(next_start + 20, static_cast<int>(images_.size()));
    std::cout << "Prefetching next region: " << next_start << " to " << next_end << std::endl;
}

void Fl_JustifiedLayout::prefetch_previous_region() {
    // Stub: Calculate and prefetch previous page of thumbnails
    int prev_end = visible_start_idx_ - 1;
    int prev_start = std::max(prev_end - 20, 0);
    std::cout << "Prefetching previous region: " << prev_start << " to " << prev_end << std::endl;
}

void Fl_JustifiedLayout::draw() {
    // Clear background
    fl_color(color());
    fl_rectf(x(), y(), w(), h());

    if (images_.empty()) {
        // Draw "no images" message
        fl_color(FL_BLACK);
        fl_font(FL_HELVETICA, 14);
        const char* msg = "No images to display. Set database or directory path.";
        int tw = 0, th = 0;
        fl_measure(msg, tw, th);
        fl_draw(msg, x() + (w() - tw) / 2, y() + (h() - th) / 2);
        return;
    }

    // Calculate visible items based on scroll offset
    calculate_layout();

    // Draw visible thumbnails
    fl_push_clip(x(), y(), w(), h());

    for (size_t i = visible_start_idx_; i <= visible_end_idx_ && i < layout_items_.size(); ++i) {
        const auto& item = layout_items_[i];
        const auto& img = images_[i];

        int item_x = x() + static_cast<int>(item.l);
        int item_y = y() + static_cast<int>(item.t - scroll_offset_);
        int item_w = static_cast<int>(item.w);
        int item_h = static_cast<int>(item.h);

        // Skip items outside visible area
        if (item_y + item_h < y() || item_y > y() + h()) continue;

        // Draw selection highlight if selected
        if (static_cast<int>(i) == selected_index_) {
            draw_selection_highlight(item_x, item_y, item_w, item_h);
        }

        // Try to draw real thumbnail first, fallback to placeholder
        draw_thumbnail_image(item_x, item_y, item_w, item_h, img);
    }

    fl_pop_clip();

    // Draw progress indicator if generating
    if (generating_.load()) {
        fl_color(FL_YELLOW);
        fl_rectf(x() + w() - 100, y() + 5, 95, 20);
        fl_color(FL_BLACK);
        fl_rect(x() + w() - 100, y() + 5, 95, 20);
        fl_font(FL_HELVETICA, 10);
        fl_draw("Generating...", x() + w() - 95, y() + 17);
    }
}

int Fl_JustifiedLayout::handle(int event) {
    switch (event) {
        case FL_PUSH:
            if (Fl::event_button() == FL_LEFT_MOUSE) {
                handle_click(Fl::event_x(), Fl::event_y());
                return 1;
            }
            break;

        case FL_MOUSEWHEEL:
            handle_scroll(Fl::event_dy() * 20);
            return 1;

        case FL_FOCUS:
        case FL_UNFOCUS:
            return 1;
    }

    return Fl_Widget::handle(event);
}

void Fl_JustifiedLayout::resize(int X, int Y, int W, int H) {
    Fl_Widget::resize(X, Y, W, H);
    layout_config_.w = W - 20; // Update layout width
    relayout();
}

void Fl_JustifiedLayout::relayout() {
    if (images_.empty()) return;

    clear_layout();
    clear_image_cache();  // Clear cache when layout changes
    calculate_layout();
    redraw();
}

void Fl_JustifiedLayout::calculate_layout() {
    if (images_.empty()) return;

    if (layout_items_.empty()) {
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
    }

    // Calculate visible range based on scroll offset
    visible_start_idx_ = 0;
    visible_end_idx_ = static_cast<int>(layout_items_.size()) - 1;

    // Visibility calculation
    bool found_first = false;
    for (size_t i = 0; i < layout_items_.size(); ++i) {
	    const auto& item = layout_items_[i];
	    int item_top = static_cast<int>(item.t - scroll_offset_);
	    int item_bottom = item_top + static_cast<int>(item.h);

	    if (item_bottom >= 0 && item_top <= h()) {
		    if (!found_first) {
			    visible_start_idx_ = static_cast<int>(i);
			    found_first = true;
		    }
		    visible_end_idx_ = static_cast<int>(i);
	    }
    }
}

void Fl_JustifiedLayout::clear_layout() {
    layout_items_.clear();
    total_height_ = 0;
}

void Fl_JustifiedLayout::clear_image_cache() {
    image_cache_.clear();
}

Fl_RGB_Image* Fl_JustifiedLayout::load_thumbnail_image(const ImageInfo& info, int target_width, int target_height) {
    // Check if we have thumbnail data
    if (info.thumb_data.empty()) {
        return nullptr;
    }

    // Create cache key based on hash and target dimensions
    std::string cache_key = info.hash + "_" + std::to_string(target_width) + "x" + std::to_string(target_height);

    // Check cache first
    auto cache_it = image_cache_.find(cache_key);
    if (cache_it != image_cache_.end()) {
        return cache_it->second.get();
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

    // Cache the image
    image_cache_[cache_key] = std::move(fltk_image);

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
    // Draw border
    fl_color(FL_GRAY);
    fl_rect(x, y, w, h);

    // Fill interior
    fl_color(FL_WHITE);
    fl_rectf(x + THUMBNAIL_BORDER_WIDTH, y + THUMBNAIL_BORDER_WIDTH,
            w - 2 * THUMBNAIL_BORDER_WIDTH, h - 2 * THUMBNAIL_BORDER_WIDTH);

    // Draw placeholder content
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
    fl_draw(filename.c_str(), x + (w - text_w) / 2, y + h / 2);

    // Draw size info
    char size_str[64];
    snprintf(size_str, sizeof(size_str), "%dx%d",
             static_cast<int>(w * info.aspect_ratio), static_cast<int>(h));
    fl_font(FL_HELVETICA, 8);
    fl_measure(size_str, text_w, text_h);
    fl_draw(size_str, x + (w - text_w) / 2, y + h / 2 + 15);
}

void Fl_JustifiedLayout::draw_selection_highlight(int x, int y, int w, int h) {
    fl_color(selection_color());
    fl_rect(x - 2, y - 2, w + 4, h + 4);
    fl_rect(x - 1, y - 1, w + 2, h + 2);
}

void Fl_JustifiedLayout::handle_click(int click_x, int click_y) {
    // Convert click coordinates to widget-relative
    int rel_x = click_x - x();
    int rel_y = click_y - y();

    // Find clicked thumbnail
    for (size_t i = visible_start_idx_; i <= visible_end_idx_ && i < layout_items_.size(); ++i) {
        const auto& item = layout_items_[i];

        int item_x = static_cast<int>(item.l);
        int item_y = static_cast<int>(item.t - scroll_offset_);
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

void Fl_JustifiedLayout::handle_scroll(int dy) {
    scroll_offset_ += dy;

    // Clamp scroll offset
    int max_scroll = std::max(0.0, total_height_ - h());
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll));

    // Trigger prefetch for visible region
    prefetch_visible_region();

    redraw();
}

bool Fl_JustifiedLayout::load_image_list() {
    images_.clear();
    clear_image_cache();  // Clear cached images when loading new list

    if (!database_) {
        std::cout << "No database available, creating mock data for testing" << std::endl;

        // Create sample images with various aspect ratios (fallback for testing)
        std::vector<std::pair<std::string, double>> sample_images = {
            {"sample1.jpg", 1.5},    // Landscape
            {"sample2.jpg", 0.75},   // Portrait
            {"sample3.jpg", 1.0},    // Square
            {"sample4.jpg", 2.0},    // Wide landscape
            {"sample5.jpg", 0.5},    // Tall portrait
            {"sample6.jpg", 1.33},   // 4:3 landscape
            {"sample7.jpg", 1.77},   // 16:9 landscape
            {"sample8.jpg", 0.56},   // 9:16 portrait
            {"sample9.jpg", 1.2},
            {"sample10.jpg", 0.8},
            {"sample11.jpg", 1.6},
            {"sample12.jpg", 0.9},
            {"sample13.jpg", 1.1},
            {"sample14.jpg", 1.8},
            {"sample15.jpg", 0.6}
        };

        for (const auto& sample : sample_images) {
            ImageInfo info;
            info.path = current_db_path_ + "/" + sample.first;
            info.hash = "mock_hash_" + std::to_string(images_.size());
            info.aspect_ratio = sample.second;
            info.best_thumb_size = 256;
            // thumb_data remains empty for mock data
            images_.push_back(info);
        }

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
