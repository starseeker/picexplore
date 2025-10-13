/*
 * fl_justified_gallery.h - FLTK justified layout gallery widget
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

#include <FL/Fl_Scroll.H>
#include <FL/Fl_RGB_Image.H>
#include <memory>
#include <vector>
#include "../justified_layout.hpp"
#include "thumbnail_cache.h"

// Widget for displaying a single image box
class Fl_Image_Box : public Fl_Widget {
public:
    Fl_Image_Box(int X, int Y, int W, int H, size_t index);
    virtual ~Fl_Image_Box();

    void draw() override;
    int handle(int event) override;

    void set_thumbnail(const ThumbnailData& thumb);
    void set_metadata(const ImageMetadata& meta);

    size_t image_index() const { return image_index_; }

private:
    size_t image_index_;
    ImageMetadata metadata_;
    ThumbnailData thumbnail_;
    std::unique_ptr<Fl_RGB_Image> fl_image_;
    bool needs_redraw_;
};

// Main gallery widget with justified layout
class Fl_Justified_Gallery : public Fl_Scroll {
public:
    Fl_Justified_Gallery(int X, int Y, int W, int H, const char* L = nullptr);
    virtual ~Fl_Justified_Gallery();

    // Initialize with thumbnail cache
    void set_cache(std::shared_ptr<ThumbnailCache> cache);

    // Layout images
    void layout_images();

    // Handle scrolling
    int handle(int event) override;
    void draw() override;

    // Scroll callback handler
    static void scroll_callback(Fl_Widget* w, void* data);

    // Timer callback for scroll stabilization
    static void scroll_timer_callback(void* data);

private:
    // Update visible images and prioritize them
    void update_visible_images();

    // Calculate which images are visible
    std::vector<size_t> get_visible_indices();

    // Rebuild layout
    void rebuild_layout();

    // Thumbnail update callback
    void on_thumbnail_ready(size_t image_index, ThumbQuality quality);

    std::shared_ptr<ThumbnailCache> cache_;
    std::vector<std::unique_ptr<Fl_Image_Box>> image_boxes_;
    
    // Layout configuration
    LayoutCfg layout_cfg_;
    std::vector<Item> layout_boxes_;
    double total_height_;

    // Scroll stabilization
    bool scroll_timer_active_;
    int last_scroll_y_;

    // Content widget that holds all image boxes
    Fl_Widget* content_;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
