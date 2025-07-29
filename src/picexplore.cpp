/*
 * picexplore.cpp - Unified image scanner, database manager, and gallery viewer
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

#include <iostream>
#include <string>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <thread>

// GUI includes
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>

// Command line parsing
#include "cxxopts.hpp"

// Core functionality
#include "database.h"
#include "pdf.h"
#include "utils.h"
#include "Fl_JustifiedLayout.h"

namespace fs = std::filesystem;

// Helper function to wrap Fl::awake call
inline void debug_awake(void (*callback)(void*), void* data, const std::string& description = "") {
    Fl::awake(callback, data);
}

namespace fs = std::filesystem;

// Forward declaration of GUI window class
class PicExploreWindow {
public:
    PicExploreWindow() : window_(nullptr), layout_widget_(nullptr) {
        create_window();
        setup_callbacks();
    }

    ~PicExploreWindow() {
        delete window_;
    }

    void show() {
        window_->show();
    }

    void run() {
        
        // Install debug handlers for various FLTK events
        setup_fltk_debug_handlers();
        
        // Run the main event loop with periodic debug status
        while (Fl::first_window()) {
            
            // Wait for and process events with timeout for debug logging
            double wait_result = Fl::wait(1.0); // Wait up to 1 second for events
            
            if (wait_result > 0) {
            } else if (wait_result == 0) {
            } else {
                break;
            }
        }
        
    }

    void set_database_path(const std::string& path) {
        if (layout_widget_) {
            if (!layout_widget_->set_database_path(path)) {
                fl_alert("Failed to load database: %s", path.c_str());
            }
        }
    }

    void set_directory_path(const std::string& path) {
        if (layout_widget_) {
            if (!layout_widget_->set_directory_path(path)) {
                fl_alert("Failed to set directory: %s", path.c_str());
            }
        }
    }

    void enable_debug_output(const std::string& dir, const std::string& format = "svg") {
        if (layout_widget_) {
            layout_widget_->enable_debug_output(dir, format);
        }
    }

private:
    void setup_fltk_debug_handlers() {
        
        // Add timeout for periodic status logging
        Fl::add_timeout(5.0, fltk_periodic_status_callback, this);
    }
    
    static void fltk_periodic_status_callback(void* data) {
        
        // Schedule next status update
        Fl::repeat_timeout(5.0, fltk_periodic_status_callback, data);
    }

    void create_window() {
        window_ = new Fl_Window(1200, 800, "PicExplore - Image Gallery and Scanner");

        // Create menu bar
        menu_bar_ = new Fl_Menu_Bar(0, 0, 1200, 25);
        menu_bar_->add("&File/Open &Database...", FL_CTRL + 'd', menu_open_database_cb, this);
        menu_bar_->add("&File/Scan D&irectory...", FL_CTRL + 'i', menu_open_directory_cb, this);
        menu_bar_->add("&File/Cancel &Scan", FL_CTRL + 'c', menu_cancel_scan_cb, this);
        menu_bar_->add("&File/Generate &PDF...", FL_CTRL + 'p', menu_generate_pdf_cb, this);
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
            // In full implementation, this would update a progress bar
        });

        // Set up selection callback
        layout_widget_->set_selection_callback([this](const std::string& path, const ImageInfo& info) {
            // In full implementation, this might show image details or full-size view
        });
    }

    // Menu callbacks
    static void menu_open_database_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        const char* path = fl_file_chooser("Select Database", "*.db", ".");
        if (path) {
            window->set_database_path(path);
        }
    }

    static void menu_open_directory_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        const char* path = fl_dir_chooser("Select Directory", ".");
        if (path) {
            window->set_directory_path(path);
        }
    }

    static void menu_cancel_scan_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->cancel_directory_scan();
        }
    }

    static void menu_generate_pdf_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        const char* path = fl_file_chooser("Save PDF As", "*.pdf", "gallery.pdf");
        if (path) {
            // TODO: Implement PDF generation from GUI
            fl_message("PDF generation from GUI not yet implemented.\nUse command line: picexplore --scan-only --pdf %s", path);
        }
    }

    static void menu_quit_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        window->window_->hide();
    }

    static void menu_start_generation_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->start_background_generation();
        }
    }

    static void menu_stop_generation_cb(Fl_Widget*, void* data) {
        PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
        if (window->layout_widget_) {
            window->layout_widget_->stop_background_generation();
        }
    }

    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;
    Fl_JustifiedLayout* layout_widget_;
};

// Function to run scan-only mode (like original picscan)
int run_scan_only_mode(int argc, char* argv[]) {
    try {
        cxxopts::Options options("picexplore", "Unified image scanner, database manager, and gallery viewer");

        options.add_options()
            ("h,help", "Print usage")
            ("scan-only", "Run in scan-only mode (no GUI)")
            ("d,directory", "Directory to scan for images", cxxopts::value<std::string>())
            ("db,database", "LMDB database path", cxxopts::value<std::string>()->default_value(get_cache_db_path()))
            ("pdf,output", "PDF output file path", cxxopts::value<std::string>())
            ("row-height", "Target row height in pixels for PDF layout", cxxopts::value<int>()->default_value("150"))
            ("margin", "Spacing between images in pixels for PDF", cxxopts::value<int>()->default_value("10"))
            ("layout-pad", "Layout padding for all sides in pixels (overridden by specific sides)", cxxopts::value<int>()->default_value("0"))
            ("layout-pad-top", "Layout padding top in pixels", cxxopts::value<int>())
            ("layout-pad-bottom", "Layout padding bottom in pixels", cxxopts::value<int>())
            ("layout-pad-left", "Layout padding left in pixels", cxxopts::value<int>())
            ("layout-pad-right", "Layout padding right in pixels", cxxopts::value<int>())
            ("debug-output", "Enable debug output to directory (creates SVG files during scanning)", cxxopts::value<std::string>())
            ("debug-format", "Debug output format: svg or png", cxxopts::value<std::string>()->default_value("svg"))
            ("v,verbose", "Enable verbose output")
        ;

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::cout << "\nExamples:\n";
            std::cout << "  # Launch GUI (default behavior):\n";
            std::cout << "  picexplore\n\n";
            std::cout << "  # Scan directory and build/update database (no GUI):\n";
            std::cout << "  picexplore --scan-only --directory /path/to/photos\n\n";
            std::cout << "  # Generate PDF from existing database (no GUI):\n";
            std::cout << "  picexplore --scan-only --pdf gallery.pdf\n\n";
            std::cout << "  # Scan directory and generate PDF in one step (no GUI):\n";
            std::cout << "  picexplore --scan-only --directory /path/to/photos --pdf gallery.pdf\n\n";
            return 0;
        }

        std::string directory = result.count("directory") ? result["directory"].as<std::string>() : "";
        std::string db_path = result["database"].as<std::string>();
        std::string pdf_path = result.count("pdf") ? result["pdf"].as<std::string>() : "";
        std::string debug_output_dir = result.count("debug-output") ? result["debug-output"].as<std::string>() : "";
        std::string debug_format = result["debug-format"].as<std::string>();
        int row_height = result["row-height"].as<int>();
        int margin = result["margin"].as<int>();
        bool verbose = result.count("verbose") > 0;

        // Parse layout padding options
        int layout_pad_default = result["layout-pad"].as<int>();
        int pad_top = result.count("layout-pad-top") ? result["layout-pad-top"].as<int>() : layout_pad_default;
        int pad_bottom = result.count("layout-pad-bottom") ? result["layout-pad-bottom"].as<int>() : layout_pad_default;
        int pad_left = result.count("layout-pad-left") ? result["layout-pad-left"].as<int>() : layout_pad_default;
        int pad_right = result.count("layout-pad-right") ? result["layout-pad-right"].as<int>() : layout_pad_default;

        // Validate arguments
        if (directory.empty() && pdf_path.empty()) {
            std::cerr << "Error: Must specify either --directory or --pdf (or both) in scan-only mode" << std::endl;
            return 1;
        }

        // Check if directory exists
        if (!directory.empty() && !fs::exists(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            return 1;
        }

        std::cout << "PicExplore - Unified Image Scanner, Database Manager, and Gallery Viewer" << std::endl;

        Timer timer;
        StatusReporter reporter(10); // Report every 10 seconds
        reporter.start();

        // Initialize database
        DatabaseManager db;
        if (!db.open(db_path)) {
            std::cerr << "Error: Failed to open database: " << db_path << std::endl;
            reporter.stop();
            return 1;
        }

        // Set up debug output callback if requested
        if (!debug_output_dir.empty()) {
            std::filesystem::create_directories(debug_output_dir);
            std::cout << "Debug output enabled: " << debug_output_dir << " (format: " << debug_format << ")" << std::endl;

            // Create a debug callback similar to our test
            class ScanDebugCallback {
            public:
                ScanDebugCallback(const std::string& dir, const std::string& format)
                    : debug_dir_(dir), format_(format), counter_(0) {}

                void operator()(const ImageInfo& info) {
                    images_.push_back(info);

                    // Write simple debug output showing progressive scanning
                    if (format_ == "svg") {
                        write_debug_svg();
                    }
                    counter_++;
                }

            private:
                void write_debug_svg() {
                    std::string filename = debug_dir_ + "/scan_progress_" + std::to_string(counter_) + ".svg";
                    std::ofstream file(filename);

                    file << "<?xml version=\"1.0\" standalone=\"no\" ?>\n";
                    file << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
                    int height = std::max(100, static_cast<int>(images_.size() * 30 + 20));
                    file << "<svg width=\"500px\" height=\"" << height << "px\" xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" >\n";

                    // Background
                    file << "\t<rect x=\"0\" y=\"0\" width=\"500\" height=\"" << height << "\" fill=\"rgb(250,250,250)\" />\n";

                    // Title
                    file << "\t<text x=\"10\" y=\"15\" fill=\"rgb(0,0,0)\" font-size=\"12\" font-family=\"Arial\">Scanning Progress: " << images_.size() << " images</text>\n";

                    // Draw a small bar for each image
                    for (size_t i = 0; i < images_.size(); ++i) {
                        const auto& img = images_[i];
                        int y = 25 + i * 30;
                        int width = static_cast<int>(img.aspect_ratio * 80);
                        width = std::min(200, std::max(10, width));

                        std::string color = img.has_thumbnails ? "rgb(100,255,100)" : "rgb(255,255,100)";

                        file << "\t<rect x=\"10\" y=\"" << y << "\" width=\"" << width << "\" height=\"20\" fill=\"" << color << "\" stroke=\"rgb(0,0,0)\" />\n";

                        // Extract filename from path for display
                        std::string filename = std::filesystem::path(img.path).filename().string();
                        if (filename.length() > 20) filename = filename.substr(0, 17) + "...";

                        file << "\t<text x=\"15\" y=\"" << (y + 14) << "\" fill=\"rgb(0,0,0)\" font-size=\"9\" font-family=\"Arial\">"
                             << filename << " (AR:" << std::fixed << std::setprecision(2) << img.aspect_ratio << ")</text>\n";
                    }

                    file << "</svg>\n";
                    file.close();
                }

                std::string debug_dir_;
                std::string format_;
                std::vector<ImageInfo> images_;
                int counter_;
            };

            static ScanDebugCallback debug_callback(debug_output_dir, debug_format);
            db.set_image_info_callback(debug_callback);
        }

        if (verbose) {
            std::cout << "Using database: " << db_path << std::endl;
        }

        bool scan_needed = !directory.empty();
        bool pdf_needed = !pdf_path.empty();

        // Phase 1: Directory scanning (if requested)
        if (scan_needed) {
            std::cout << "Scanning directory: " << directory << std::endl;

            int processed = db.scan_directory_parallel(directory, timer, reporter);
            if (processed < 0) {
                std::cerr << "Error: Failed to scan directory" << std::endl;
                reporter.stop();
                return 1;
            }

            std::cout << "Processed " << processed << " images" << std::endl;
        }

        // Phase 2: PDF generation (if requested)
        if (pdf_needed) {
            timer.start("Database Query");
            reporter.update_status("Loading images from database...");

            std::vector<ImageInfo> images = db.get_all_images();

            timer.stop("Database Query");

            if (images.empty()) {
                std::cerr << "Error: No images found in database";
                if (!scan_needed) {
                    std::cerr << " (try scanning a directory first)";
                }
                std::cerr << std::endl;
                reporter.stop();
                return 1;
            }

            std::cout << "Generating PDF with " << images.size() << " images: " << pdf_path << std::endl;

            // Create PDFOptions with CLI arguments
            PDFOptions pdf_options;
            pdf_options.row_height = row_height;
            pdf_options.margin = margin;
            pdf_options.pad_top = pad_top;
            pdf_options.pad_bottom = pad_bottom;
            pdf_options.pad_left = pad_left;
            pdf_options.pad_right = pad_right;

            PDFGenerator pdf_gen;
            if (!pdf_gen.generate_pdf(images, pdf_path, timer, reporter, pdf_options)) {
                std::cerr << "Error: Failed to generate PDF" << std::endl;
                reporter.stop();
                return 1;
            }

            std::cout << "Successfully generated PDF: " << pdf_path << std::endl;
        }

        reporter.stop();

        // Print timing summary
        timer.print_summary();

        std::cout << "\nOperation completed successfully!" << std::endl;
        return 0;

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}

// Function to run GUI mode
int run_gui_mode(int argc, char* argv[]) {
    // Parse any initial arguments for GUI mode
    std::string initial_database;
    std::string initial_directory;

    // Simple parsing for GUI mode - only look for -d/--database and -i/--directory
    std::string debug_output_dir;
    std::string debug_output_format = "svg";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            std::cout << "PicExplore - Unified Image Scanner, Database Manager, and Gallery Viewer\n";
            std::cout << "\nUsage: picexplore [OPTIONS]\n";
            std::cout << "\nOptions:\n";
            std::cout << "  -h, --help               Show this help message\n";
            std::cout << "  -d, --database PATH      Open LMDB database at PATH\n";
            std::cout << "  -i, --directory PATH     Open directory PATH (will scan/build database)\n";
            std::cout << "  --debug-output DIR       Enable debug output to DIR (creates SVG files)\n";
            std::cout << "  --debug-format FORMAT    Debug output format: svg or png (default: svg)\n";
            std::cout << "  --scan-only              Run in scan-only mode (no GUI) - see --help for scan options\n";
            std::cout << "\nExamples:\n";
            std::cout << "  picexplore                           # Launch GUI\n";
            std::cout << "  picexplore --database " << get_cache_db_path(true) << "    # Launch GUI with specific database\n";
            std::cout << "  picexplore --directory ~/Pictures    # Launch GUI and scan directory\n";
            std::cout << "  picexplore --directory ~/Pictures --debug-output /tmp/debug  # With debug output\n";
            std::cout << "  picexplore --scan-only --help        # Show scan-only mode options\n";
            return 0;
        }
        else if ((arg == "-d" || arg == "--database") && i + 1 < argc) {
            initial_database = argv[++i];
        }
        else if ((arg == "-i" || arg == "--directory") && i + 1 < argc) {
            initial_directory = argv[++i];
        }
        else if (arg == "--debug-output" && i + 1 < argc) {
            debug_output_dir = argv[++i];
        }
        else if (arg == "--debug-format" && i + 1 < argc) {
            debug_output_format = argv[++i];
        }
        else if (arg != "--scan-only") {  // Ignore --scan-only, handled elsewhere
            std::cerr << "Unknown argument in GUI mode: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
    }

    // Create and show main window
    PicExploreWindow app;

    // Enable debug output if requested
    if (!debug_output_dir.empty()) {
        app.enable_debug_output(debug_output_dir, debug_output_format);
    }

    // Load initial content if specified
    if (!initial_database.empty()) {
        app.set_database_path(initial_database);
    } else if (!initial_directory.empty()) {
        app.set_directory_path(initial_directory);
    }

    app.show();

    std::cout << "PicExplore GUI started. Use File menu to load images." << std::endl;
    std::cout << "Click thumbnails to select, use mouse wheel to scroll." << std::endl;

    app.run();

    return 0;
}

int main(int argc, char* argv[]) {

    Fl::lock();

    // Determine mode based on command line arguments
    bool scan_only_mode = false;


    // Check for --scan-only option
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--scan-only") {
            scan_only_mode = true;
            break;
        }
    }

    if (scan_only_mode) {
        return run_scan_only_mode(argc, argv);
    } else {
        return run_gui_mode(argc, argv);
    }
}
