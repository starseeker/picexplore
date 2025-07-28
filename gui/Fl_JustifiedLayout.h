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
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include "../database.h"
#include "../justified_layout.hpp"

// Forward declarations
class DatabaseManager;

// Progress callback for background thumbnail generation
using ProgressCallback = std::function<void(int current, int total, const std::string& status)>;

// Selection callback for thumbnail clicks
using SelectionCallback = std::function<void(const std::string& image_path, const ImageInfo& info)>;

/**
 * FLTK widget that displays image thumbnails using justified layout algorithm.
 * 
 * Features (skeleton implementation):
 * - Displays placeholder boxes for images in justified layout
 * - API to set LMDB database path
 * - Async thumbnail generation queues (stubbed)
 * - Progress indication (stubbed) 
 * - Selection callbacks for thumbnail interaction
 * - Scrollable view with prefetch support (stubbed)
 */
class Fl_JustifiedLayout : public Fl_Widget {
public:
    // Constructor
    Fl_JustifiedLayout(int X, int Y, int W, int H, const char* label = nullptr);
    
    // Destructor
    virtual ~Fl_JustifiedLayout();
    
    // Database management
    bool set_database_path(const std::string& db_path);
    bool set_directory_path(const std::string& dir_path); // Will scan/build new database
    
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
    
    // Thumbnail rendering (placeholder implementation)
    void draw_thumbnail_placeholder(int x, int y, int w, int h, const ImageInfo& info);
    void draw_selection_highlight(int x, int y, int w, int h);
    
    // Event handling
    void handle_click(int x, int y);
    void handle_scroll(int dy);
    
    // Database operations
    bool load_image_list();
    
private:
    // Database and image management
    std::unique_ptr<DatabaseManager> database_;
    std::vector<ImageInfo> images_;
    std::string current_db_path_;
    
    // Layout calculation
    LayoutCfg layout_config_;
    std::vector<LayoutItem> layout_items_;
    double total_height_;
    int visible_start_idx_;
    int visible_end_idx_;
    
    // Selection state
    int selected_index_;
    
    // Async generation state (stubbed)
    std::atomic<bool> generating_;
    std::atomic<bool> should_stop_;
    
    // Callbacks
    ProgressCallback progress_callback_;
    SelectionCallback selection_callback_;
    
    // Scroll state
    int scroll_offset_;
    
    // Constants
    static constexpr int THUMBNAIL_BORDER_WIDTH = 2;
    static constexpr int DEFAULT_ROW_HEIGHT = 150;
    static constexpr int MIN_THUMBNAIL_SIZE = 50;
};