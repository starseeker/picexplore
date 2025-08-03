/*
 * justified_layout_view.hpp - View-only justified layout widget for MVC architecture
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
#include <FL/Fl_Widget.H>
#include <FL/Fl_RGB_Image.H>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include "justified_layout.hpp"

// Forward declarations
class GalleryController;
class Fl_JustifiedLayout_View_Content;

/**
 * Display item for a single image in the gallery
 */
struct GalleryDisplayItem {
    std::string hash;
    std::string path;
    double aspect_ratio = 1.0;
    bool is_selected = false;
    bool has_thumbnail = false;
    std::shared_ptr<Fl_RGB_Image> thumbnail_image;
    
    // Layout position (set by layout algorithm)
    int x = 0;
    int y = 0; 
    int width = 0;
    int height = 0;
};

/**
 * Simplified view-only justified layout widget
 * Handles only display and user interaction, delegates business logic to controller
 */
class Fl_JustifiedLayout_View : public Fl_Scroll {
public:
    Fl_JustifiedLayout_View(int X, int Y, int W, int H, const char* label = nullptr);
    virtual ~Fl_JustifiedLayout_View();

    /**
     * Set the controller for this view
     */
    void set_controller(std::shared_ptr<GalleryController> controller);

    /**
     * Set display items (called by controller)
     */
    void set_display_items(const std::vector<GalleryDisplayItem>& items);

    /**
     * Update thumbnail for specific item
     */
    void update_thumbnail(const std::string& hash, std::shared_ptr<Fl_RGB_Image> thumbnail);

    /**
     * Set selected item
     */
    void set_selected_item(int index);

    /**
     * Get current viewport range
     */
    void get_viewport_range(int& start_index, int& end_index) const;

    /**
     * Configure layout settings
     */
    void set_layout_config(double row_height, double spacing_h, double spacing_v, 
                          double pad_top, double pad_right, double pad_bottom, double pad_left);

    /**
     * Force redraw of the widget
     */
    void refresh_display();

    // FLTK widget overrides
    void draw() override;
    int handle(int event) override;
    void resize(int X, int Y, int W, int H) override;

private:
    std::shared_ptr<GalleryController> controller_;
    Fl_JustifiedLayout_View_Content* content_widget_;
    
    // Display data
    std::vector<GalleryDisplayItem> display_items_;
    int selected_index_;
    
    // Layout configuration
    LayoutCfg layout_config_;
    double total_height_;
    
    // Viewport tracking
    int visible_start_idx_;
    int visible_end_idx_;
    
    // Private methods
    void calculate_layout();
    void update_viewport_info();
    void handle_click(int x, int y);
    void notify_viewport_change();
    
    friend class Fl_JustifiedLayout_View_Content;
};

/**
 * Content widget for the scrollable area
 */
class Fl_JustifiedLayout_View_Content : public Fl_Widget {
public:
    Fl_JustifiedLayout_View_Content(int X, int Y, int W, int H, Fl_JustifiedLayout_View* parent);
    
    void draw() override;
    int handle(int event) override;

private:
    Fl_JustifiedLayout_View* parent_;
    
    // Drawing methods
    void draw_thumbnail_placeholder(int x, int y, int w, int h, const GalleryDisplayItem& item);
    void draw_thumbnail_image(int x, int y, int w, int h, const GalleryDisplayItem& item);
    void draw_selection_highlight(int x, int y, int w, int h);
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s