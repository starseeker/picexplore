/*
 * gui_test.cpp - Minimal test application for Fl_JustifiedLayout widget
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>
#include <iostream>
#include <string>
#include "Fl_JustifiedLayout.h"

class ImageExplorerWindow {
public:
    ImageExplorerWindow() : window_(nullptr), layout_widget_(nullptr) {
        create_window();
        setup_callbacks();
    }

    ~ImageExplorerWindow() {
        delete window_;
    }

    void show() {
        window_->show();
    }

    void run() {
        Fl::run();
    }

    void set_database_path(const std::string& path) {
        if (layout_widget_) {
            if (layout_widget_->set_database_path(path)) {
                std::cout << "Successfully loaded database: " << path << std::endl;
            } else {
                fl_alert("Failed to load database: %s", path.c_str());
            }
        }
    }

    void set_directory_path(const std::string& path) {
        if (layout_widget_) {
            if (layout_widget_->set_directory_path(path)) {
                std::cout << "Successfully set directory: " << path << std::endl;
            } else {
                fl_alert("Failed to set directory: %s", path.c_str());
            }
        }
    }

private:
    void create_window() {
        window_ = new Fl_Window(1200, 800, "PicExplore - Image Thumbnail Viewer");

        // Create menu bar
        menu_bar_ = new Fl_Menu_Bar(0, 0, 1200, 25);
        menu_bar_->add("&File/Open &Database...", FL_CTRL + 'd', menu_open_database_cb, this);
        menu_bar_->add("&File/Scan D&irectory...", FL_CTRL + 'i', menu_open_directory_cb, this);
        menu_bar_->add("&File/Cancel &Scan", FL_CTRL + 'c', menu_cancel_scan_cb, this);
        menu_bar_->add("&File/&Quit", FL_CTRL + 'q', menu_quit_cb, this);
        menu_bar_->add("&View/Start &Background Generation", FL_CTRL + 'g', menu_start_generation_cb, this);
        menu_bar_->add("&View/&Stop Background Generation", FL_CTRL + 's', menu_stop_generation_cb, this);

        // Create main layout widget
        layout_widget_ = new Fl_JustifiedLayout(10, 35, 1180, 750);

        window_->end();
        window_->resizable(layout_widget_);
    }

    void setup_callbacks() {
        if (!layout_widget_) return;

        // Set up progress callback
        layout_widget_->set_progress_callback([this](int current, int total, const std::string& status) {
            std::cout << "Progress: " << current << "/" << total << " - " << status << std::endl;
            // In full implementation, this would update a progress bar
        });

        // Set up selection callback
        layout_widget_->set_selection_callback([this](const std::string& path, const ImageInfo& info) {
            std::cout << "Selected: " << path << " (aspect: " << info.aspect_ratio << ")" << std::endl;
            // In full implementation, this might show image details or full-size view
        });
    }

    // Menu callbacks
    static void menu_open_database_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        const char* path = fl_file_chooser("Select Database", "*.db", ".");
        if (path) {
            window->set_database_path(path);
        }
    }

    static void menu_open_directory_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        const char* path = fl_dir_chooser("Select Directory", ".");
        if (path) {
            window->set_directory_path(path);
        }
    }

    static void menu_cancel_scan_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->cancel_directory_scan();
        }
    }

    static void menu_quit_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        window->window_->hide();
    }

    static void menu_start_generation_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->start_background_generation();
        }
    }

    static void menu_stop_generation_cb(Fl_Widget*, void* data) {
        ImageExplorerWindow* window = static_cast<ImageExplorerWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->stop_background_generation();
        }
    }

    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;
    Fl_JustifiedLayout* layout_widget_;
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "\nOptions:\n";
    std::cout << "  -h, --help               Show this help message\n";
    std::cout << "  -d, --database PATH      Open LMDB database at PATH\n";
    std::cout << "  -i, --directory PATH     Open directory PATH (will scan/build database)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << program_name << " --database ./images.db\n";
    std::cout << "  " << program_name << " --directory ~/Pictures\n";
    std::cout << "  " << program_name << "  # Open empty window, use File menu to load content\n";
}

int main(int argc, char** argv) {
    std::string database_path;
    std::string directory_path;

    // Simple command line argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if ((arg == "-d" || arg == "--database") && i + 1 < argc) {
            database_path = argv[++i];
        }
        else if ((arg == "-i" || arg == "--directory") && i + 1 < argc) {
            directory_path = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Create and show main window
    ImageExplorerWindow app;

    // Load initial content if specified
    if (!database_path.empty()) {
        app.set_database_path(database_path);
    } else if (!directory_path.empty()) {
        app.set_directory_path(directory_path);
    }

    app.show();

    std::cout << "PicExplore GUI started. Use File menu to load images." << std::endl;
    std::cout << "Click thumbnails to select, use mouse wheel to scroll." << std::endl;

    app.run();

    return 0;
}