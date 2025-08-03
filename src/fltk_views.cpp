/*
 * fltk_views.cpp - FLTK-based view implementations
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "fltk_views.hpp"
#include "justified_layout_view.hpp"
#include <FL/Fl.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <iostream>

//=============================================================================
// FLTKGalleryView Implementation
//=============================================================================

FLTKGalleryView::FLTKGalleryView(int x, int y, int w, int h) 
    : x_(x), y_(y), w_(w), h_(h), layout_widget_(nullptr) {
}

FLTKGalleryView::~FLTKGalleryView() {
    // layout_widget_ will be cleaned up by FLTK
}

bool FLTKGalleryView::initialize() {
    layout_widget_ = new Fl_JustifiedLayout_View(x_, y_, w_, h_);
    if (!layout_widget_) {
        return false;
    }
    
    layout_widget_->set_controller(controller_);
    return true;
}

void FLTKGalleryView::show() {
    if (layout_widget_) {
        layout_widget_->show();
    }
}

void FLTKGalleryView::hide() {
    if (layout_widget_) {
        layout_widget_->hide();
    }
}

void FLTKGalleryView::update_display() {
    if (!layout_widget_ || !controller_) {
        return;
    }
    
    // Get image data from controller and convert to display items
    std::vector<GalleryDisplayItem> display_items;
    size_t image_count = controller_->get_image_count();
    int selected_index = controller_->get_selected_image_index();
    
    display_items.reserve(image_count);
    
    for (size_t i = 0; i < image_count; ++i) {
        auto image_state = controller_->get_image_state(static_cast<int>(i));
        if (image_state) {
            GalleryDisplayItem item;
            item.hash = image_state->metadata.hash;
            item.path = image_state->metadata.path;
            item.aspect_ratio = image_state->metadata.aspect_ratio;
            item.is_selected = (static_cast<int>(i) == selected_index);
            item.has_thumbnail = image_state->thumbnails_generated;
            // TODO: Get actual thumbnail image from state store
            display_items.push_back(item);
        }
    }
    
    layout_widget_->set_display_items(display_items);
    
    // Update selection
    layout_widget_->set_selected_item(selected_index);
}

void FLTKGalleryView::set_controller(std::shared_ptr<GalleryController> controller) {
    controller_ = controller;
    if (layout_widget_) {
        layout_widget_->set_controller(controller);
    }
}

void FLTKGalleryView::on_thumbnail_selected(int image_index) {
    if (controller_) {
        controller_->select_image(image_index);
    }
}

void FLTKGalleryView::on_viewport_changed(int start_index, int end_index) {
    if (controller_) {
        // Request thumbnails for visible items
        for (int i = start_index; i <= end_index; ++i) {
            controller_->request_thumbnail(i, 200, 200); // Request 200x200 thumbnails
        }
    }
}

void FLTKGalleryView::get_viewport_info(int& start_index, int& end_index) const {
    if (layout_widget_) {
        layout_widget_->get_viewport_range(start_index, end_index);
    } else {
        start_index = end_index = 0;
    }
}

void FLTKGalleryView::request_redraw() {
    if (layout_widget_) {
        layout_widget_->refresh_display();
    }
}

//=============================================================================
// FLTKPicExploreView Implementation
//=============================================================================

FLTKPicExploreView::FLTKPicExploreView() 
    : window_(nullptr), menu_bar_(nullptr) {
}

FLTKPicExploreView::~FLTKPicExploreView() {
    if (window_) {
        delete window_;
    }
}

bool FLTKPicExploreView::initialize() {
    create_window();
    setup_menu();
    
    // Create gallery view
    gallery_view_ = std::make_shared<FLTKGalleryView>(10, 35, 1180, 750);
    if (!gallery_view_->initialize()) {
        return false;
    }
    
    return true;
}

void FLTKPicExploreView::show() {
    if (window_) {
        window_->show();
    }
}

void FLTKPicExploreView::hide() {
    if (window_) {
        window_->hide();
    }
}

void FLTKPicExploreView::update_display() {
    if (gallery_view_) {
        gallery_view_->update_display();
    }
}

void FLTKPicExploreView::set_controller(std::shared_ptr<ApplicationController> controller) {
    controller_ = controller;
    
    // Set controller for gallery view
    if (gallery_view_ && controller) {
        gallery_view_->set_controller(controller->get_gallery_controller());
    }
}

std::shared_ptr<GalleryView> FLTKPicExploreView::get_gallery_view() {
    return gallery_view_;
}

void FLTKPicExploreView::handle_scan_completion(const ScanResult& result) {
    if (result.success) {
        show_message("Scan Complete", 
                    "Successfully processed " + std::to_string(result.images_processed) + " images");
    } else {
        show_error("Scan Failed", result.error_message);
    }
}

void FLTKPicExploreView::handle_scan_progress(int current, int total, const std::string& status) {
    // TODO: Update progress display in status bar or progress dialog
    std::cout << "Scan progress: " << current << "/" << total << " - " << status << std::endl;
}

void FLTKPicExploreView::show_directory_chooser(std::function<void(const std::string&)> callback) {
    const char* path = fl_dir_chooser("Select Directory", ".");
    if (callback) {
        callback(path ? std::string(path) : std::string());
    }
}

void FLTKPicExploreView::show_pdf_save_dialog(std::function<void(const std::string&)> callback) {
    const char* path = fl_file_chooser("Save PDF As", "*.pdf", "gallery.pdf");
    if (callback) {
        callback(path ? std::string(path) : std::string());
    }
}

void FLTKPicExploreView::show_message(const std::string& title, const std::string& message) {
    fl_message("%s", message.c_str());
}

void FLTKPicExploreView::show_error(const std::string& title, const std::string& error) {
    fl_alert("%s", error.c_str());
}

void FLTKPicExploreView::run() {
    // Run the main FLTK event loop
    while (Fl::first_window()) {
        double wait_result = Fl::wait(1.0); // Wait up to 1 second for events
        if (wait_result < 0) {
            break;
        }
    }
}

void FLTKPicExploreView::exit() {
    if (window_) {
        window_->hide();
    }
}

void FLTKPicExploreView::create_window() {
    window_ = new Fl_Window(1200, 800, "PicExplore - Image Gallery and Scanner");
    window_->color(FL_WHITE);
}

void FLTKPicExploreView::setup_menu() {
    if (!window_) return;
    
    menu_bar_ = new Fl_Menu_Bar(0, 0, 1200, 25);
    menu_bar_->add("&File/Scan D&irectory...", FL_CTRL + 'i', menu_open_directory_cb, this);
    menu_bar_->add("&File/Cancel &Scan", FL_CTRL + 'c', menu_cancel_scan_cb, this);
    menu_bar_->add("&File/Generate &PDF...", FL_CTRL + 'p', menu_generate_pdf_cb, this);
    menu_bar_->add("&File/&Quit", FL_CTRL + 'q', menu_quit_cb, this);
    
    window_->end();
    window_->resizable(window_);
}

// Static menu callbacks
void FLTKPicExploreView::menu_open_directory_cb(Fl_Widget*, void* data) {
    FLTKPicExploreView* view = static_cast<FLTKPicExploreView*>(data);
    view->handle_open_directory();
}

void FLTKPicExploreView::menu_cancel_scan_cb(Fl_Widget*, void* data) {
    FLTKPicExploreView* view = static_cast<FLTKPicExploreView*>(data);
    view->handle_cancel_scan();
}

void FLTKPicExploreView::menu_generate_pdf_cb(Fl_Widget*, void* data) {
    FLTKPicExploreView* view = static_cast<FLTKPicExploreView*>(data);
    view->handle_generate_pdf();
}

void FLTKPicExploreView::menu_quit_cb(Fl_Widget*, void* data) {
    FLTKPicExploreView* view = static_cast<FLTKPicExploreView*>(data);
    view->handle_quit();
}

// Menu action handlers
void FLTKPicExploreView::handle_open_directory() {
    show_directory_chooser([this](const std::string& path) {
        if (!path.empty() && controller_) {
            controller_->open_directory(path);
        }
    });
}

void FLTKPicExploreView::handle_cancel_scan() {
    if (controller_) {
        controller_->get_scan_controller()->cancel_current_scan();
    }
}

void FLTKPicExploreView::handle_generate_pdf() {
    show_pdf_save_dialog([this](const std::string& path) {
        if (!path.empty() && controller_) {
            PDFGenerationConfig config;
            config.output_path = path;
            
            controller_->generate_pdf(config, [this](bool success, const std::string& message) {
                if (success) {
                    show_message("PDF Generation", message);
                } else {
                    show_error("PDF Generation Failed", message);
                }
            });
        }
    });
}

void FLTKPicExploreView::handle_quit() {
    if (controller_) {
        controller_->exit_application();
    }
    exit();
}

//=============================================================================
// FLTKViewFactory Implementation
//=============================================================================

std::shared_ptr<PicExploreView> FLTKViewFactory::create_main_view() {
    return std::make_shared<FLTKPicExploreView>();
}

std::shared_ptr<GalleryView> FLTKViewFactory::create_gallery_view() {
    return std::make_shared<FLTKGalleryView>(0, 0, 800, 600);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s