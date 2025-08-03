/*
 * justified_layout_view.cpp - View-only justified layout widget implementation
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "justified_layout_view.hpp"
#include "controllers.hpp"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <algorithm>
#include <iostream>

//=============================================================================
// Fl_JustifiedLayout_View Implementation
//=============================================================================

Fl_JustifiedLayout_View::Fl_JustifiedLayout_View(int X, int Y, int W, int H, const char* label)
    : Fl_Scroll(X, Y, W, H, label)
    , content_widget_(nullptr)
    , selected_index_(-1)
    , total_height_(0)
    , visible_start_idx_(0)
    , visible_end_idx_(0)
{
    // Initialize layout configuration with reasonable defaults
    layout_config_.w = W - 20; // Leave some margin for scrollbar
    layout_config_.rh = 150;   // Default row height
    layout_config_.pt = layout_config_.pr = layout_config_.pb = layout_config_.pl = 10;
    layout_config_.sh = layout_config_.sv = 5;

    // Set widget appearance
    color(FL_WHITE);
    selection_color(FL_BLUE);
    type(Fl_Scroll::VERTICAL);  // Only vertical scrolling

    // Create content widget
    content_widget_ = new Fl_JustifiedLayout_View_Content(X, Y, W, 100, this);
    end(); // Finalize children
}

Fl_JustifiedLayout_View::~Fl_JustifiedLayout_View() {
    // content_widget_ will be deleted by FLTK's widget destructor
}

void Fl_JustifiedLayout_View::set_controller(std::shared_ptr<GalleryController> controller) {
    controller_ = controller;
}

void Fl_JustifiedLayout_View::set_display_items(const std::vector<GalleryDisplayItem>& items) {
    display_items_ = items;
    
    // Reset selection if out of range
    if (selected_index_ >= static_cast<int>(display_items_.size())) {
        selected_index_ = -1;
    }
    
    calculate_layout();
    refresh_display();
}

void Fl_JustifiedLayout_View::update_thumbnail(const std::string& hash, std::shared_ptr<Fl_RGB_Image> thumbnail) {
    // Find item with matching hash and update thumbnail
    for (auto& item : display_items_) {
        if (item.hash == hash) {
            item.thumbnail_image = thumbnail;
            item.has_thumbnail = (thumbnail != nullptr);
            break;
        }
    }
    
    // Redraw the affected area
    refresh_display();
}

void Fl_JustifiedLayout_View::set_selected_item(int index) {
    if (index >= -1 && index < static_cast<int>(display_items_.size())) {
        selected_index_ = index;
        
        // Update selection state in display items
        for (size_t i = 0; i < display_items_.size(); ++i) {
            display_items_[i].is_selected = (static_cast<int>(i) == selected_index_);
        }
        
        refresh_display();
    }
}

void Fl_JustifiedLayout_View::get_viewport_range(int& start_index, int& end_index) const {
    start_index = visible_start_idx_;
    end_index = visible_end_idx_;
}

void Fl_JustifiedLayout_View::set_layout_config(double row_height, double spacing_h, double spacing_v,
                                               double pad_top, double pad_right, double pad_bottom, double pad_left) {
    layout_config_.rh = row_height;
    layout_config_.sh = spacing_h;
    layout_config_.sv = spacing_v;
    layout_config_.pt = pad_top;
    layout_config_.pr = pad_right;
    layout_config_.pb = pad_bottom;
    layout_config_.pl = pad_left;
    
    calculate_layout();
    refresh_display();
}

void Fl_JustifiedLayout_View::refresh_display() {
    if (content_widget_) {
        content_widget_->redraw();
    }
}

void Fl_JustifiedLayout_View::calculate_layout() {
    // Convert display items to layout items
    std::vector<Item> input_items;
    input_items.reserve(display_items_.size());
    
    for (const auto& item : display_items_) {
        Item layout_item;
        layout_item.ar = item.aspect_ratio;
        input_items.push_back(layout_item);
    }
    
    // Update layout width based on current widget size
    layout_config_.w = w() - 20; // Account for scrollbar
    
    // Calculate justified layout using the JustifiedLayout class
    if (!input_items.empty()) {
        JustifiedLayout layout(input_items, layout_config_);
        
        // Get the resulting boxes from the layout
        const auto& boxes = layout.boxes();
        
        // Apply layout results back to display items
        for (size_t i = 0; i < std::min(display_items_.size(), boxes.size()); ++i) {
            display_items_[i].x = static_cast<int>(boxes[i].l);      // left -> x
            display_items_[i].y = static_cast<int>(boxes[i].t);      // top -> y
            display_items_[i].width = static_cast<int>(boxes[i].w);  // width
            display_items_[i].height = static_cast<int>(boxes[i].h); // height
        }
        
        // Use the calculated total height from the layout
        total_height_ = layout.height();
    } else {
        total_height_ = 0;
    }
    
    // Resize content widget to match calculated height
    if (content_widget_) {
        content_widget_->resize(x(), y(), w(), static_cast<int>(total_height_));
    }
    
    update_viewport_info();
}

void Fl_JustifiedLayout_View::update_viewport_info() {
    if (display_items_.empty()) {
        visible_start_idx_ = visible_end_idx_ = 0;
        return;
    }
    
    int scroll_y = yposition();
    int viewport_height = h();
    int viewport_top = scroll_y;
    int viewport_bottom = scroll_y + viewport_height;
    
    // Find first visible item
    visible_start_idx_ = static_cast<int>(display_items_.size());
    for (size_t i = 0; i < display_items_.size(); ++i) {
        const auto& item = display_items_[i];
        if (item.y + item.height >= viewport_top) {
            visible_start_idx_ = static_cast<int>(i);
            break;
        }
    }
    
    // Find last visible item
    visible_end_idx_ = 0;
    for (size_t i = 0; i < display_items_.size(); ++i) {
        const auto& item = display_items_[i];
        if (item.y <= viewport_bottom) {
            visible_end_idx_ = static_cast<int>(i);
        } else {
            break;
        }
    }
    
    // Ensure valid range
    visible_start_idx_ = std::max(0, std::min(visible_start_idx_, static_cast<int>(display_items_.size()) - 1));
    visible_end_idx_ = std::max(0, std::min(visible_end_idx_, static_cast<int>(display_items_.size()) - 1));
}

void Fl_JustifiedLayout_View::handle_click(int click_x, int click_y) {
    // Convert click coordinates to content coordinates
    int content_x = click_x;
    int content_y = click_y + yposition();
    
    // Find clicked item
    for (size_t i = 0; i < display_items_.size(); ++i) {
        const auto& item = display_items_[i];
        if (content_x >= item.x && content_x < item.x + item.width &&
            content_y >= item.y && content_y < item.y + item.height) {
            
            // Notify controller of selection
            if (controller_) {
                controller_->select_image(static_cast<int>(i));
            }
            break;
        }
    }
}

void Fl_JustifiedLayout_View::notify_viewport_change() {
    if (controller_) {
        // Notify controller when viewport changes so it can request thumbnails
        // for visible items and manage caching
        update_viewport_info();
        // Note: In a full implementation, controller would handle thumbnail requests
    }
}

void Fl_JustifiedLayout_View::draw() {
    // Let the scroll widget handle its own drawing
    Fl_Scroll::draw();
}

int Fl_JustifiedLayout_View::handle(int event) {
    int result = Fl_Scroll::handle(event);
    
    // Check for viewport changes on scroll events
    if (event == FL_PUSH || event == FL_DRAG || event == FL_MOUSEWHEEL) {
        int old_start = visible_start_idx_;
        int old_end = visible_end_idx_;
        update_viewport_info();
        
        if (old_start != visible_start_idx_ || old_end != visible_end_idx_) {
            notify_viewport_change();
        }
    }
    
    return result;
}

void Fl_JustifiedLayout_View::resize(int X, int Y, int W, int H) {
    Fl_Scroll::resize(X, Y, W, H);
    calculate_layout(); // Recalculate layout when widget is resized
}

//=============================================================================
// Fl_JustifiedLayout_View_Content Implementation
//=============================================================================

Fl_JustifiedLayout_View_Content::Fl_JustifiedLayout_View_Content(int X, int Y, int W, int H, 
                                                                 Fl_JustifiedLayout_View* parent)
    : Fl_Widget(X, Y, W, H), parent_(parent) {
}

void Fl_JustifiedLayout_View_Content::draw() {
    if (!parent_) return;
    
    // Get viewport information
    int scroll_y = parent_->yposition();
    int viewport_height = parent_->h();
    int viewport_top = scroll_y;
    int viewport_bottom = scroll_y + viewport_height;
    
    // Draw background
    fl_color(FL_WHITE);
    fl_rectf(x(), y(), w(), h());
    
    // Draw only visible items for performance
    for (const auto& item : parent_->display_items_) {
        // Skip items outside viewport
        if (item.y + item.height < viewport_top || item.y > viewport_bottom) {
            continue;
        }
        
        int draw_x = x() + item.x;
        int draw_y = y() + item.y;
        
        if (item.has_thumbnail && item.thumbnail_image) {
            draw_thumbnail_image(draw_x, draw_y, item.width, item.height, item);
        } else {
            draw_thumbnail_placeholder(draw_x, draw_y, item.width, item.height, item);
        }
        
        if (item.is_selected) {
            draw_selection_highlight(draw_x, draw_y, item.width, item.height);
        }
    }
}

int Fl_JustifiedLayout_View_Content::handle(int event) {
    if (!parent_) return 0;
    
    if (event == FL_PUSH) {
        parent_->handle_click(Fl::event_x() - x(), Fl::event_y() - y());
        return 1;
    }
    
    return Fl_Widget::handle(event);
}

void Fl_JustifiedLayout_View_Content::draw_thumbnail_placeholder(int x, int y, int w, int h, 
                                                                const GalleryDisplayItem& item) {
    // Draw placeholder rectangle
    fl_color(FL_LIGHT2);
    fl_rectf(x, y, w, h);
    
    // Draw border
    fl_color(FL_DARK3);
    fl_rect(x, y, w, h);
    
    // Draw loading indicator or filename
    fl_color(FL_BLACK);
    fl_font(FL_HELVETICA, 12);
    
    std::string filename = item.path;
    size_t last_slash = filename.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        filename = filename.substr(last_slash + 1);
    }
    
    // Truncate filename if too long
    if (filename.length() > 20) {
        filename = filename.substr(0, 17) + "...";
    }
    
    int text_w = 0, text_h = 0;
    fl_measure(filename.c_str(), text_w, text_h);
    
    int text_x = x + (w - text_w) / 2;
    int text_y = y + (h - text_h) / 2 + text_h;
    
    fl_draw(filename.c_str(), text_x, text_y);
}

void Fl_JustifiedLayout_View_Content::draw_thumbnail_image(int x, int y, int w, int h, 
                                                          const GalleryDisplayItem& item) {
    if (!item.thumbnail_image) return;
    
    // Draw the thumbnail image, scaling to fit the rectangle
    Fl_RGB_Image* img = item.thumbnail_image.get();
    
    // Calculate scaling to fit while maintaining aspect ratio
    double scale_x = static_cast<double>(w) / img->w();
    double scale_y = static_cast<double>(h) / img->h();
    double scale = std::min(scale_x, scale_y);
    
    int scaled_w = static_cast<int>(img->w() * scale);
    int scaled_h = static_cast<int>(img->h() * scale);
    
    // Center the image in the rectangle
    int img_x = x + (w - scaled_w) / 2;
    int img_y = y + (h - scaled_h) / 2;
    
    // Create a temporary scaled image for drawing
    Fl_RGB_Image* scaled_img = static_cast<Fl_RGB_Image*>(img->copy(scaled_w, scaled_h));
    if (scaled_img) {
        scaled_img->draw(img_x, img_y);
        delete scaled_img;
    } else {
        // Fallback: draw original image
        img->draw(img_x, img_y);
    }
    
    // Draw border around image
    fl_color(FL_DARK3);
    fl_rect(x, y, w, h);
}

void Fl_JustifiedLayout_View_Content::draw_selection_highlight(int x, int y, int w, int h) {
    // Draw selection highlight
    fl_color(FL_BLUE);
    fl_line_style(FL_SOLID, 3);
    fl_rect(x, y, w, h);
    fl_line_style(FL_SOLID, 1); // Reset line style
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s