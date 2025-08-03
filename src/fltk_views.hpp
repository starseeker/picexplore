/*
 * fltk_views.hpp - FLTK-based view implementations for picexplore
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

#include "views.hpp"
#include "controllers.hpp"
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <memory>

// Forward declarations
class Fl_JustifiedLayout_View;

/**
 * FLTK implementation of the gallery view
 * Wraps the existing Fl_JustifiedLayout but separates view from controller logic
 */
class FLTKGalleryView : public GalleryView {
public:
    FLTKGalleryView(int x, int y, int w, int h);
    virtual ~FLTKGalleryView();

    // IView implementation
    bool initialize() override;
    void show() override;
    void hide() override;
    void update_display() override;

    // GalleryView implementation
    void set_controller(std::shared_ptr<GalleryController> controller) override;
    void on_thumbnail_selected(int image_index) override;
    void on_viewport_changed(int start_index, int end_index) override;
    void get_viewport_info(int& start_index, int& end_index) const override;
    void request_redraw() override;

    /**
     * Get the underlying FLTK widget for embedding in other widgets
     */
    Fl_JustifiedLayout_View* get_fltk_widget() { return layout_widget_; }

private:
    std::shared_ptr<GalleryController> controller_;
    Fl_JustifiedLayout_View* layout_widget_;
    int x_, y_, w_, h_;
};

/**
 * FLTK implementation of the main application view
 */
class FLTKPicExploreView : public PicExploreView {
public:
    FLTKPicExploreView();
    virtual ~FLTKPicExploreView();

    // IView implementation
    bool initialize() override;
    void show() override;
    void hide() override;
    void update_display() override;

    // PicExploreView implementation
    void set_controller(std::shared_ptr<ApplicationController> controller) override;
    std::shared_ptr<GalleryView> get_gallery_view() override;
    void handle_scan_completion(const ScanResult& result) override;
    void handle_scan_progress(int current, int total, const std::string& status) override;
    void show_directory_chooser(std::function<void(const std::string&)> callback) override;
    void show_pdf_save_dialog(std::function<void(const std::string&)> callback) override;
    void show_message(const std::string& title, const std::string& message) override;
    void show_error(const std::string& title, const std::string& error) override;
    void run() override;
    void exit() override;

private:
    std::shared_ptr<ApplicationController> controller_;
    std::shared_ptr<FLTKGalleryView> gallery_view_;
    
    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;

    // Menu callbacks (static methods for FLTK)
    static void menu_open_directory_cb(Fl_Widget*, void* data);
    static void menu_cancel_scan_cb(Fl_Widget*, void* data);
    static void menu_generate_pdf_cb(Fl_Widget*, void* data);
    static void menu_quit_cb(Fl_Widget*, void* data);

    void create_window();
    void setup_menu();
    
    // Handle menu actions
    void handle_open_directory();
    void handle_cancel_scan();
    void handle_generate_pdf();
    void handle_quit();
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s