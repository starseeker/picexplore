/*
 * fl_justified_gallery.cpp - FLTK justified layout gallery implementation
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

#include "fl_justified_gallery.h"
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include <jpeglib.h>
#include <setjmp.h>
#include <iostream>

// ========== Fl_Image_Box Implementation ==========

Fl_Image_Box::Fl_Image_Box(int X, int Y, int W, int H, size_t index)
    : Fl_Widget(X, Y, W, H)
    , image_index_(index)
    , needs_redraw_(false)
{
}

Fl_Image_Box::~Fl_Image_Box()
{
}

void Fl_Image_Box::set_thumbnail(const ThumbnailData& thumb)
{
    thumbnail_ = thumb;
    
    // Decode JPEG data into RGB
    if (!thumb.jpeg_data.empty()) {
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;
        
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);
        
        jpeg_mem_src(&cinfo, thumb.jpeg_data.data(), thumb.jpeg_data.size());
        jpeg_read_header(&cinfo, TRUE);
        jpeg_start_decompress(&cinfo);
        
        int width = cinfo.output_width;
        int height = cinfo.output_height;
        int channels = cinfo.output_components;
        
        std::vector<unsigned char> rgb_data(width * height * channels);
        int row_stride = width * channels;
        
        while (cinfo.output_scanline < cinfo.output_height) {
            unsigned char* row = rgb_data.data() + cinfo.output_scanline * row_stride;
            jpeg_read_scanlines(&cinfo, &row, 1);
        }
        
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        
        // Create FLTK image
        fl_image_.reset(new Fl_RGB_Image(rgb_data.data(), width, height, channels));
        fl_image_->alloc_array = 1;  // Let FLTK manage the data
        
        // Copy data for FLTK to manage
        unsigned char* fltk_data = new unsigned char[rgb_data.size()];
        std::copy(rgb_data.begin(), rgb_data.end(), fltk_data);
        fl_image_.reset(new Fl_RGB_Image(fltk_data, width, height, channels));
    }
    
    needs_redraw_ = true;
    redraw();
}

void Fl_Image_Box::set_metadata(const ImageMetadata& meta)
{
    metadata_ = meta;
}

void Fl_Image_Box::draw()
{
    // Draw border
    fl_color(FL_BLACK);
    fl_rect(x(), y(), w(), h());
    
    if (fl_image_) {
        // Scale image to fit within box
        int img_w = fl_image_->w();
        int img_h = fl_image_->h();
        
        // Calculate scaling to fit
        double scale_x = (double)(w() - 2) / img_w;
        double scale_y = (double)(h() - 2) / img_h;
        double scale = std::min(scale_x, scale_y);
        
        int draw_w = (int)(img_w * scale);
        int draw_h = (int)(img_h * scale);
        
        // Center in box
        int draw_x = x() + (w() - draw_w) / 2;
        int draw_y = y() + (h() - draw_h) / 2;
        
        // Draw scaled image
        Fl_Image* scaled = fl_image_->copy(draw_w, draw_h);
        scaled->draw(draw_x, draw_y);
        delete scaled;
    } else {
        // Draw grey rectangle as placeholder
        fl_color(FL_GRAY);
        fl_rectf(x() + 1, y() + 1, w() - 2, h() - 2);
        
        // Draw resolution text
        if (metadata_.width > 0 && metadata_.height > 0) {
            fl_color(FL_BLACK);
            char buf[64];
            snprintf(buf, sizeof(buf), "%dx%d", metadata_.width, metadata_.height);
            fl_draw(buf, x() + 5, y() + 15);
        }
    }
}

int Fl_Image_Box::handle(int event)
{
    switch (event) {
        case FL_PUSH:
            std::cout << "Clicked image: " << metadata_.filepath << std::endl;
            return 1;
        default:
            return Fl_Widget::handle(event);
    }
}

// ========== Fl_Justified_Gallery Implementation ==========

Fl_Justified_Gallery::Fl_Justified_Gallery(int X, int Y, int W, int H, const char* L)
    : Fl_Scroll(X, Y, W, H, L)
    , total_height_(0)
    , scroll_timer_active_(false)
    , last_scroll_y_(0)
{
    // Configure layout
    layout_cfg_.w = W - 20;  // Account for scrollbar
    layout_cfg_.rh = 150;    // Target row height
    layout_cfg_.sh = 5;      // Horizontal spacing
    layout_cfg_.sv = 5;      // Vertical spacing
    layout_cfg_.pl = 5;      // Padding left
    layout_cfg_.pr = 5;      // Padding right
    layout_cfg_.pt = 5;      // Padding top
    layout_cfg_.pb = 5;      // Padding bottom
    
    type(VERTICAL);
    box(FL_FLAT_BOX);
}

Fl_Justified_Gallery::~Fl_Justified_Gallery()
{
}

void Fl_Justified_Gallery::set_cache(std::shared_ptr<ThumbnailCache> cache)
{
    cache_ = cache;
    
    // Set callback for thumbnail updates
    cache_->set_callback([this](size_t idx, ThumbQuality quality) {
        // This is called from worker thread - use Fl::awake to update UI
        Fl::awake([](void* data) {
            auto* pair = static_cast<std::pair<Fl_Justified_Gallery*, size_t>*>(data);
            Fl_Justified_Gallery* gallery = pair->first;
            size_t idx = pair->second;
            
            // Update the specific image box
            if (idx < gallery->image_boxes_.size()) {
                auto& box = gallery->image_boxes_[idx];
                ThumbnailData thumb = gallery->cache_->get_thumbnail(idx);
                box->set_thumbnail(thumb);
            }
            
            delete pair;
        }, new std::pair<Fl_Justified_Gallery*, size_t>(this, idx));
    });
    
    layout_images();
}

void Fl_Justified_Gallery::layout_images()
{
    if (!cache_)
        return;
    
    rebuild_layout();
    update_visible_images();
}

void Fl_Justified_Gallery::rebuild_layout()
{
    if (!cache_)
        return;
    
    // Clear existing boxes
    image_boxes_.clear();
    
    // Build item list for layout
    std::vector<Item> items;
    size_t count = cache_->image_count();
    
    for (size_t i = 0; i < count; ++i) {
        ImageMetadata meta = cache_->get_metadata(i);
        Item item;
        item.ar = meta.aspect_ratio;
        items.push_back(item);
    }
    
    if (items.empty())
        return;
    
    // Calculate layout
    JustifiedLayout layout(items, layout_cfg_);
    layout_boxes_ = layout.boxes();
    total_height_ = layout.height() + layout_cfg_.pb;
    
    // Create image boxes
    for (size_t i = 0; i < layout_boxes_.size() && i < count; ++i) {
        const Item& box = layout_boxes_[i];
        
        int bx = x() + (int)box.l;
        int by = y() + (int)box.t;
        int bw = (int)box.w;
        int bh = (int)box.h;
        
        auto image_box = std::make_unique<Fl_Image_Box>(bx, by, bw, bh, i);
        
        // Set metadata
        ImageMetadata meta = cache_->get_metadata(i);
        image_box->set_metadata(meta);
        
        // Set current thumbnail if available
        ThumbnailData thumb = cache_->get_thumbnail(i);
        if (thumb.quality != ThumbQuality::NONE) {
            image_box->set_thumbnail(thumb);
        }
        
        image_boxes_.push_back(std::move(image_box));
    }
}

void Fl_Justified_Gallery::update_visible_images()
{
    if (!cache_)
        return;
    
    std::vector<size_t> visible = get_visible_indices();
    cache_->prioritize_visible_images(visible);
}

std::vector<size_t> Fl_Justified_Gallery::get_visible_indices()
{
    std::vector<size_t> visible;
    
    int scroll_y = yposition();
    int view_h = h();
    
    for (size_t i = 0; i < image_boxes_.size(); ++i) {
        const auto& box = image_boxes_[i];
        int box_y = box->y() - y();
        int box_h = box->h();
        
        // Check if box intersects with visible area
        if (box_y + box_h >= scroll_y && box_y <= scroll_y + view_h) {
            visible.push_back(i);
        }
    }
    
    return visible;
}

int Fl_Justified_Gallery::handle(int event)
{
    // Handle scrolling
    int result = Fl_Scroll::handle(event);
    
    if (event == FL_MOUSEWHEEL || event == FL_DRAG) {
        int current_y = yposition();
        
        if (current_y != last_scroll_y_) {
            last_scroll_y_ = current_y;
            
            // Cancel existing timer
            if (scroll_timer_active_) {
                Fl::remove_timeout(scroll_timer_callback, this);
            }
            
            // Start new timer (0.5 seconds)
            Fl::add_timeout(0.5, scroll_timer_callback, this);
            scroll_timer_active_ = true;
        }
    }
    
    // Forward events to children
    if (event == FL_PUSH) {
        for (auto& box : image_boxes_) {
            if (Fl::event_inside(box.get())) {
                box->handle(event);
                return 1;
            }
        }
    }
    
    return result;
}

void Fl_Justified_Gallery::draw()
{
    Fl_Scroll::draw();
    
    // Draw all visible image boxes
    int scroll_y = yposition();
    int view_h = h();
    
    fl_push_clip(x(), y(), w(), h());
    
    for (auto& box : image_boxes_) {
        int box_y = box->y() - y();
        
        // Only draw visible boxes
        if (box_y + box->h() >= scroll_y && box_y <= scroll_y + view_h) {
            box->draw();
        }
    }
    
    fl_pop_clip();
}

void Fl_Justified_Gallery::scroll_timer_callback(void* data)
{
    Fl_Justified_Gallery* gallery = static_cast<Fl_Justified_Gallery*>(data);
    gallery->scroll_timer_active_ = false;
    gallery->update_visible_images();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
