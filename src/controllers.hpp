/*
 * controllers.hpp - Controller layer for picexplore MVC architecture
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
#include "state_store.hpp"
#include "thread_manager.hpp"
#include "database.hpp"

// Forward declarations
class PicExploreView;
class GalleryView;

/**
 * Base controller interface for common controller functionality
 */
class IController {
public:
    virtual ~IController() = default;
    
    /**
     * Initialize the controller with necessary dependencies
     */
    virtual bool initialize() = 0;
    
    /**
     * Shutdown and cleanup controller resources
     */
    virtual void shutdown() = 0;
};

/**
 * Scan operation results and callbacks
 */
struct ScanResult {
    bool success = false;
    int images_processed = 0;
    std::string error_message;
};

using ScanCompletionCallback = std::function<void(const ScanResult&)>;
using ScanProgressCallback = std::function<void(int current, int total, const std::string& status)>;

/**
 * Controller for directory scanning operations
 * Handles all business logic related to scanning directories and managing database operations
 */
class ScanController : public IController {
public:
    ScanController(std::shared_ptr<StateStore> state_store, 
                   std::shared_ptr<ThreadManager> thread_manager);
    virtual ~ScanController();

    // IController implementation
    bool initialize() override;
    void shutdown() override;

    /**
     * Start scanning a directory
     * @param directory_path Path to directory to scan
     * @param completion_callback Called when scan completes or fails
     * @param progress_callback Called periodically during scan progress
     */
    bool start_directory_scan(const std::string& directory_path,
                             ScanCompletionCallback completion_callback = nullptr,
                             ScanProgressCallback progress_callback = nullptr);

    /**
     * Cancel current scanning operation
     */
    void cancel_current_scan();

    /**
     * Check if a scan is currently in progress
     */
    bool is_scanning() const;

    /**
     * Get current scan progress information
     */
    ScanState get_scan_state() const;

private:
    std::shared_ptr<StateStore> state_store_;
    std::shared_ptr<ThreadManager> thread_manager_;
    
    ScanCompletionCallback completion_callback_;
    ScanProgressCallback progress_callback_;
    
    uint64_t scan_progress_subscription_id_ = 0;
    
    // Event handlers
    void handle_scan_progress_event(const StateEvent& event);
    void handle_scan_completion_event(const StateEvent& event);
};

/**
 * Gallery display configuration and state
 */
struct GalleryDisplayConfig {
    int row_height = 150;
    int spacing_horizontal = 10;
    int spacing_vertical = 10;
    double padding_top = 0;
    double padding_right = 0;
    double padding_bottom = 0;
    double padding_left = 0;
};

/**
 * Controller for gallery view operations
 * Handles business logic for image display, selection, and interaction
 */
class GalleryController : public IController {
public:
    GalleryController(std::shared_ptr<StateStore> state_store,
                      std::shared_ptr<ThreadManager> thread_manager);
    virtual ~GalleryController();

    // IController implementation  
    bool initialize() override;
    void shutdown() override;

    /**
     * Set the view component this controller manages
     */
    void set_view(GalleryView* view);

    /**
     * Load images for display from the current state
     */
    void load_images();

    /**
     * Handle image selection by user
     * @param image_index Index of selected image
     */
    void select_image(int image_index);

    /**
     * Get currently selected image index
     */
    int get_selected_image_index() const;

    /**
     * Update gallery display configuration
     */
    void update_display_config(const GalleryDisplayConfig& config);

    /**
     * Get current display configuration
     */
    GalleryDisplayConfig get_display_config() const;

    /**
     * Request thumbnail for specific image at target size
     */
    void request_thumbnail(int image_index, int target_width, int target_height);

    /**
     * Get image count
     */
    size_t get_image_count() const;

    /**
     * Get image metadata by index
     */
    std::shared_ptr<const ImageState> get_image_state(int index) const;

private:
    std::shared_ptr<StateStore> state_store_;
    std::shared_ptr<ThreadManager> thread_manager_;
    GalleryView* view_;
    
    std::vector<std::string> image_hashes_;  // Ordered list of image hashes for display
    int selected_index_;
    GalleryDisplayConfig display_config_;
    
    uint64_t image_metadata_subscription_id_ = 0;
    uint64_t thumbnail_ready_subscription_id_ = 0;
    
    // Event handlers
    void handle_image_metadata_event(const StateEvent& event);
    void handle_thumbnail_ready_event(const StateEvent& event);
    
    // Update view with current state
    void update_view();
};

/**
 * PDF generation configuration
 */
struct PDFGenerationConfig {
    std::string output_path;
    int row_height = 150;
    int margin = 10;
    int layout_pad_top = 0;
    int layout_pad_bottom = 0;
    int layout_pad_left = 0;
    int layout_pad_right = 0;
};

using PDFGenerationCallback = std::function<void(bool success, const std::string& message)>;

/**
 * Main application controller
 * Coordinates between different controllers and manages overall application flow
 */
class ApplicationController : public IController {
public:
    ApplicationController();
    virtual ~ApplicationController();

    // IController implementation
    bool initialize() override;
    void shutdown() override;

    /**
     * Set the main application view
     */
    void set_main_view(PicExploreView* view);

    /**
     * Get scan controller
     */
    std::shared_ptr<ScanController> get_scan_controller() { return scan_controller_; }

    /**
     * Get gallery controller  
     */
    std::shared_ptr<GalleryController> get_gallery_controller() { return gallery_controller_; }

    /**
     * Open and scan a directory
     */
    bool open_directory(const std::string& directory_path);

    /**
     * Open an existing database
     */
    bool open_database(const std::string& database_path);

    /**
     * Generate PDF from current images
     */
    void generate_pdf(const PDFGenerationConfig& config, PDFGenerationCallback callback = nullptr);

    /**
     * Get current state store
     */
    std::shared_ptr<StateStore> get_state_store() { return state_store_; }

    /**
     * Exit application
     */
    void exit_application();

private:
    std::shared_ptr<StateStore> state_store_;
    std::shared_ptr<ThreadManager> thread_manager_;
    std::shared_ptr<ScanController> scan_controller_;
    std::shared_ptr<GalleryController> gallery_controller_;
    
    PicExploreView* main_view_;
    
    // Handle application-level events
    void handle_scan_completion(const ScanResult& result);
    void handle_scan_progress(int current, int total, const std::string& status);
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s