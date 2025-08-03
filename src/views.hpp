/*
 * views.hpp - View layer interfaces for picexplore MVC architecture
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

#include <string>
#include <memory>
#include <functional>

// Forward declarations
struct ScanResult;
class ApplicationController;
class GalleryController;

/**
 * Base interface for all views in the application
 */
class IView {
public:
    virtual ~IView() = default;
    
    /**
     * Initialize the view
     */
    virtual bool initialize() = 0;
    
    /**
     * Show the view
     */
    virtual void show() = 0;
    
    /**
     * Hide the view
     */
    virtual void hide() = 0;
    
    /**
     * Update the view display
     */
    virtual void update_display() = 0;
};

/**
 * Interface for gallery view component
 * Responsible only for displaying thumbnails and handling user interaction
 */
class GalleryView : public IView {
public:
    virtual ~GalleryView() = default;
    
    /**
     * Set the controller for this view
     */
    virtual void set_controller(std::shared_ptr<GalleryController> controller) = 0;
    
    /**
     * Handle user thumbnail selection
     * @param image_index Index of selected thumbnail
     */
    virtual void on_thumbnail_selected(int image_index) = 0;
    
    /**
     * Handle user scroll/viewport changes
     * @param start_index First visible image index
     * @param end_index Last visible image index
     */
    virtual void on_viewport_changed(int start_index, int end_index) = 0;
    
    /**
     * Get current viewport information
     */
    virtual void get_viewport_info(int& start_index, int& end_index) const = 0;
    
    /**
     * Request view to redraw/refresh
     */
    virtual void request_redraw() = 0;
};

/**
 * Interface for main application view
 * Responsible for overall UI layout and coordination
 */
class PicExploreView : public IView {
public:
    virtual ~PicExploreView() = default;
    
    /**
     * Set the application controller
     */
    virtual void set_controller(std::shared_ptr<ApplicationController> controller) = 0;
    
    /**
     * Get the gallery view component
     */
    virtual std::shared_ptr<GalleryView> get_gallery_view() = 0;
    
    /**
     * Handle scan completion notification from controller
     */
    virtual void handle_scan_completion(const ScanResult& result) = 0;
    
    /**
     * Handle scan progress updates from controller
     */
    virtual void handle_scan_progress(int current, int total, const std::string& status) = 0;
    
    /**
     * Show directory selection dialog
     * @param callback Called with selected directory path (empty if cancelled)
     */
    virtual void show_directory_chooser(std::function<void(const std::string&)> callback) = 0;
    
    /**
     * Show file save dialog for PDF
     * @param callback Called with selected file path (empty if cancelled)
     */
    virtual void show_pdf_save_dialog(std::function<void(const std::string&)> callback) = 0;
    
    /**
     * Show message to user
     */
    virtual void show_message(const std::string& title, const std::string& message) = 0;
    
    /**
     * Show error message to user
     */
    virtual void show_error(const std::string& title, const std::string& error) = 0;
    
    /**
     * Run the main UI event loop
     */
    virtual void run() = 0;
    
    /**
     * Exit the application
     */
    virtual void exit() = 0;
};

/**
 * Factory interface for creating views
 * Allows different UI implementations (FLTK, Qt, etc.)
 */
class ViewFactory {
public:
    virtual ~ViewFactory() = default;
    
    /**
     * Create main application view
     */
    virtual std::shared_ptr<PicExploreView> create_main_view() = 0;
    
    /**
     * Create standalone gallery view
     */
    virtual std::shared_ptr<GalleryView> create_gallery_view() = 0;
};

/**
 * FLTK-based view factory implementation
 */
class FLTKViewFactory : public ViewFactory {
public:
    std::shared_ptr<PicExploreView> create_main_view() override;
    std::shared_ptr<GalleryView> create_gallery_view() override;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s