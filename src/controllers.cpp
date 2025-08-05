/*
 * controllers.cpp - Controller layer implementation for picexplore MVC architecture
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

#include "controllers.hpp"
#include "views.hpp"
#include "pdf.hpp"
#include "utils.hpp"
#include "plugin_manager.hpp"
#include <iostream>
#include <algorithm>

//=============================================================================
// ScanController Implementation
//=============================================================================

ScanController::ScanController(std::shared_ptr<StateStore> state_store, 
                               std::shared_ptr<ThreadManager> thread_manager)
    : state_store_(state_store), thread_manager_(thread_manager) {
}

ScanController::~ScanController() {
    shutdown();
}

bool ScanController::initialize() {
    if (!state_store_ || !thread_manager_) {
        return false;
    }

    // Subscribe to scan progress events
    scan_progress_subscription_id_ = state_store_->get_event_bus().subscribe(
        StateEventType::SCAN_PROGRESS,
        [this](const StateEvent& event) { handle_scan_progress_event(event); }
    );

    // Subscribe to scan completion events
    auto completion_id = state_store_->get_event_bus().subscribe(
        StateEventType::SCAN_COMPLETED,
        [this](const StateEvent& event) { handle_scan_completion_event(event); }
    );

    auto cancel_id = state_store_->get_event_bus().subscribe(
        StateEventType::SCAN_CANCELLED,
        [this](const StateEvent& event) { handle_scan_completion_event(event); }
    );

    return true;
}

void ScanController::shutdown() {
    if (state_store_) {
        if (scan_progress_subscription_id_ != 0) {
            state_store_->get_event_bus().unsubscribe(scan_progress_subscription_id_);
            scan_progress_subscription_id_ = 0;
        }
    }
    
    cancel_current_scan();
}

bool ScanController::start_directory_scan(const std::string& directory_path,
                                         ScanCompletionCallback completion_callback,
                                         ScanProgressCallback progress_callback) {
    if (is_scanning()) {
        return false;  // Already scanning
    }

    completion_callback_ = completion_callback;
    progress_callback_ = progress_callback;

    // Start scan using thread manager
    return thread_manager_->start_directory_scan(directory_path);
}

void ScanController::cancel_current_scan() {
    if (thread_manager_) {
        thread_manager_->cancel_scan();
    }
}

bool ScanController::is_scanning() const {
    return thread_manager_ ? thread_manager_->is_scanning() : false;
}

ScanState ScanController::get_scan_state() const {
    return state_store_ ? state_store_->get_scan_state() : ScanState{};
}

void ScanController::handle_scan_progress_event(const StateEvent& event) {
    if (progress_callback_) {
        const ScanEvent* scan_event = static_cast<const ScanEvent*>(&event);
        progress_callback_(scan_event->current_count, scan_event->total_count, scan_event->status_message);
    }
}

void ScanController::handle_scan_completion_event(const StateEvent& event) {
    if (completion_callback_) {
        ScanResult result;
        result.success = (event.type == StateEventType::SCAN_COMPLETED);
        
        if (event.type == StateEventType::SCAN_COMPLETED) {
            const ScanEvent* scan_event = static_cast<const ScanEvent*>(&event);
            result.images_processed = scan_event->current_count;
        } else {
            result.error_message = "Scan was cancelled";
        }
        
        completion_callback_(result);
    }
}

//=============================================================================
// GalleryController Implementation
//=============================================================================

GalleryController::GalleryController(std::shared_ptr<StateStore> state_store,
                                     std::shared_ptr<ThreadManager> thread_manager)
    : state_store_(state_store), thread_manager_(thread_manager), 
      view_(nullptr), selected_index_(-1) {
}

GalleryController::~GalleryController() {
    shutdown();
}

bool GalleryController::initialize() {
    if (!state_store_ || !thread_manager_) {
        return false;
    }

    // Subscribe to image metadata events
    image_metadata_subscription_id_ = state_store_->get_event_bus().subscribe(
        StateEventType::IMAGE_METADATA_ADDED,
        [this](const StateEvent& event) { handle_image_metadata_event(event); }
    );

    // Subscribe to thumbnail ready events
    thumbnail_ready_subscription_id_ = state_store_->get_event_bus().subscribe(
        StateEventType::THUMBNAIL_READY,
        [this](const StateEvent& event) { handle_thumbnail_ready_event(event); }
    );

    return true;
}

void GalleryController::shutdown() {
    if (state_store_) {
        if (image_metadata_subscription_id_ != 0) {
            state_store_->get_event_bus().unsubscribe(image_metadata_subscription_id_);
            image_metadata_subscription_id_ = 0;
        }
        if (thumbnail_ready_subscription_id_ != 0) {
            state_store_->get_event_bus().unsubscribe(thumbnail_ready_subscription_id_);
            thumbnail_ready_subscription_id_ = 0;
        }
    }
}

void GalleryController::set_view(GalleryView* view) {
    view_ = view;
}

void GalleryController::load_images() {
    if (!state_store_) return;

    // Get all images from state store
    auto images = state_store_->get_all_images();
    
    // Build ordered list of image hashes
    image_hashes_.clear();
    image_hashes_.reserve(images.size());
    
    for (const auto& image : images) {
        image_hashes_.push_back(image.metadata.hash);
    }
    
    // Sort by path for consistent ordering
    std::sort(image_hashes_.begin(), image_hashes_.end(), 
              [this](const std::string& a, const std::string& b) {
                  auto state_a = state_store_->get_image_state(a);
                  auto state_b = state_store_->get_image_state(b);
                  if (state_a && state_b) {
                      return state_a->metadata.path < state_b->metadata.path;
                  }
                  return a < b;
              });

    update_view();
}

void GalleryController::select_image(int image_index) {
    if (image_index >= 0 && image_index < static_cast<int>(image_hashes_.size())) {
        selected_index_ = image_index;
        update_view();
    }
}

int GalleryController::get_selected_image_index() const {
    return selected_index_;
}

void GalleryController::update_display_config(const GalleryDisplayConfig& config) {
    display_config_ = config;
    update_view();
}

GalleryDisplayConfig GalleryController::get_display_config() const {
    return display_config_;
}

void GalleryController::request_thumbnail(int image_index, int target_width, int target_height) {
    if (!thread_manager_ || image_index < 0 || image_index >= static_cast<int>(image_hashes_.size())) {
        return;
    }

    const std::string& hash = image_hashes_[image_index];
    auto image_state = state_store_->get_image_state(hash);
    
    if (image_state) {
        UIThumbnailTask task(image_index, UIThumbnailTask::Priority::HIGH, 
                           target_width, target_height, hash, image_state->metadata.path);
        thread_manager_->request_thumbnail(task);
    }
}

size_t GalleryController::get_image_count() const {
    return image_hashes_.size();
}

std::shared_ptr<const ImageState> GalleryController::get_image_state(int index) const {
    if (index < 0 || index >= static_cast<int>(image_hashes_.size())) {
        return nullptr;
    }
    
    return state_store_->get_image_state(image_hashes_[index]);
}

void GalleryController::handle_image_metadata_event(const StateEvent& event) {
    // Reload images when new metadata is added
    load_images();
}

void GalleryController::handle_thumbnail_ready_event(const StateEvent& event) {
    // Notify view that thumbnail is ready
    update_view();
}

void GalleryController::update_view() {
    if (view_) {
        view_->update_display();
    }
}

//=============================================================================
// ApplicationController Implementation
//=============================================================================

ApplicationController::ApplicationController() 
    : main_view_(nullptr) {
}

ApplicationController::~ApplicationController() {
    shutdown();
}

bool ApplicationController::initialize() {
    // Create shared state store and thread manager
    state_store_ = std::make_shared<StateStore>();
    thread_manager_ = std::make_shared<ThreadManager>();

    // Create and initialize plugin manager
    plugin_manager_ = std::make_shared<PluginManager>(
        &state_store_->get_event_bus(), 
        state_store_.get(), 
        thread_manager_.get()
    );
    
    if (!plugin_manager_->initialize()) {
        std::cerr << "Failed to initialize plugin manager" << std::endl;
        return false;
    }

    // Create and initialize sub-controllers
    scan_controller_ = std::make_shared<ScanController>(state_store_, thread_manager_);
    gallery_controller_ = std::make_shared<GalleryController>(state_store_, thread_manager_);

    if (!scan_controller_->initialize()) {
        std::cerr << "Failed to initialize scan controller" << std::endl;
        return false;
    }

    if (!gallery_controller_->initialize()) {
        std::cerr << "Failed to initialize gallery controller" << std::endl;
        return false;
    }

    // Initialize plugins after controllers are ready
    initialize_plugins();

    return true;
}

void ApplicationController::shutdown() {
    // Shutdown plugin manager first
    if (plugin_manager_) {
        plugin_manager_->shutdown();
    }
    if (scan_controller_) {
        scan_controller_->shutdown();
    }
    if (gallery_controller_) {
        gallery_controller_->shutdown();
    }
    if (thread_manager_) {
        thread_manager_->shutdown_all();
    }
}

void ApplicationController::set_main_view(PicExploreView* view) {
    main_view_ = view;
}

bool ApplicationController::open_directory(const std::string& directory_path) {
    if (!scan_controller_) {
        return false;
    }

    // Set up callbacks for scan progress and completion
    auto progress_callback = [this](int current, int total, const std::string& status) {
        handle_scan_progress(current, total, status);
    };

    auto completion_callback = [this](const ScanResult& result) {
        handle_scan_completion(result);
    };

    return scan_controller_->start_directory_scan(directory_path, completion_callback, progress_callback);
}

bool ApplicationController::open_database(const std::string& database_path) {
    // TODO: Implement database opening logic
    // For now, just trigger gallery reload
    if (gallery_controller_) {
        gallery_controller_->load_images();
        return true;
    }
    return false;
}

void ApplicationController::generate_pdf(const PDFGenerationConfig& config, PDFGenerationCallback callback) {
    if (!state_store_) {
        if (callback) {
            callback(false, "State store not available");
        }
        return;
    }

    // Get all images from state store
    auto image_states = state_store_->get_all_images();
    
    if (image_states.empty()) {
        if (callback) {
            callback(false, "No images found in database");
        }
        return;
    }

    // Convert to ImageInfo vector for PDF generation
    std::vector<ImageInfo> images;
    images.reserve(image_states.size());
    
    for (const auto& state : image_states) {
        ImageInfo info;
        info.path = state.metadata.path;
        info.hash = state.metadata.hash;
        info.aspect_ratio = state.metadata.aspect_ratio;
        info.best_thumb_size = state.metadata.best_thumb_size;
        info.thumb_data = state.metadata.thumb_data;
        info.thumb_width = state.metadata.thumb_width;
        info.thumb_height = state.metadata.thumb_height;
        info.has_thumbnails = state.metadata.has_thumbnails;
        images.push_back(info);
    }

    // Create PDF options from config
    PDFOptions pdf_options;
    pdf_options.row_height = config.row_height;
    pdf_options.margin = config.margin;
    pdf_options.pad_top = config.layout_pad_top;
    pdf_options.pad_bottom = config.layout_pad_bottom;
    pdf_options.pad_left = config.layout_pad_left;
    pdf_options.pad_right = config.layout_pad_right;

    // Generate PDF
    PDFGenerator pdf_gen;
    Timer timer;
    StatusReporter reporter(1); // Report every second

    reporter.start();
    reporter.update_status("Generating PDF...");

    bool success = pdf_gen.generate_pdf(images, config.output_path, timer, reporter, pdf_options);

    reporter.stop();

    if (callback) {
        if (success) {
            callback(true, "PDF generated successfully: " + config.output_path);
        } else {
            callback(false, "Failed to generate PDF");
        }
    }
}

void ApplicationController::exit_application() {
    shutdown();
}

void ApplicationController::handle_scan_completion(const ScanResult& result) {
    if (main_view_) {
        main_view_->handle_scan_completion(result);
    }

    // Load images into gallery after successful scan
    if (result.success && gallery_controller_) {
        gallery_controller_->load_images();
    }
}

void ApplicationController::handle_scan_progress(int current, int total, const std::string& status) {
    if (main_view_) {
        main_view_->handle_scan_progress(current, total, status);
    }
}

bool ApplicationController::initialize_plugins() {
    if (!plugin_manager_) {
        return false;
    }
    
    // Register hooks first to catch plugin registrations
    plugin_manager_->register_extension_hook(ExtensionPointType::METADATA_EXTRACTOR,
        [](IExtensionPoint* extension) {
            auto* extractor = dynamic_cast<IMetadataExtractor*>(extension);
            if (extractor) {
                std::cout << "Registered metadata extractor: " << extractor->get_name() 
                         << " with capabilities: ";
                for (const auto& cap : extractor->get_capabilities()) {
                    std::cout << cap << " ";
                }
                std::cout << std::endl;
            }
        });
    
    plugin_manager_->register_extension_hook(ExtensionPointType::IMAGE_PROCESSOR,
        [](IExtensionPoint* extension) {
            auto* processor = dynamic_cast<IImageProcessor*>(extension);
            if (processor) {
                std::cout << "Registered image processor: " << processor->get_name() 
                         << " with capabilities: ";
                for (const auto& cap : processor->get_capabilities()) {
                    std::cout << cap << " ";
                }
                std::cout << std::endl;
            }
        });
    
    // Get default plugin directories and scan for plugins
    auto plugin_dirs = plugin_manager_->get_default_plugin_directories();
    
    for (const auto& dir : plugin_dirs) {
        auto results = plugin_manager_->scan_and_load_plugins(dir);
        
        for (const auto& result : results) {
            if (result.is_success()) {
                std::cout << "Loaded plugin: " << result.plugin_name << std::endl;
            } else if (result.status != PluginLoadStatus::FILE_NOT_FOUND) {
                // Don't warn about missing directories, but warn about other issues
                std::cerr << "Plugin load warning: " << result.message << std::endl;
            }
        }
    }
    
    return true;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s