/*
 * Fl_JustifiedLayout.cpp - FLTK widget for displaying thumbnails in justified layout
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "Fl_JustifiedLayout.h"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <iostream>
#include <algorithm>

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
}

bool Fl_JustifiedLayout::set_database_path(const std::string& db_path) {
    current_db_path_ = db_path;
    
    // For now, create a mock database with some sample images
    // In the full implementation, this would open the LMDB database
    database_ = std::make_unique<DatabaseManager>();
    
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
        
        // Draw thumbnail placeholder
        draw_thumbnail_placeholder(item_x, item_y, item_w, item_h, img);
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
    // For skeleton implementation, create some mock image data
    images_.clear();
    
    // Create sample images with various aspect ratios
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
        images_.push_back(info);
    }
    
    std::cout << "Loaded " << images_.size() << " sample images" << std::endl;
    
    // Trigger layout calculation
    relayout();
    
    return true;
}
