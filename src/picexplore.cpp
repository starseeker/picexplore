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
#include "database.hpp"
#include "pdf.hpp"
#include "utils.hpp"
#include "Fl_JustifiedLayout.hpp"
#include "thread_manager.hpp"

namespace fs = std::filesystem;

// Helper function to wrap Fl::awake call
inline void debug_awake(void (*callback)(void*), void* data, const std::string& description = "") {
    Fl::awake(callback, data);
}

namespace fs = std::filesystem;

// Forward declaration of GUI window class
class PicExploreWindow {
    public:
	PicExploreWindow() : window_(nullptr), layout_widget_(nullptr), thread_manager_(std::make_unique<ThreadManager>()) {
	    create_window();
	    setup_callbacks();
	    setup_thread_callbacks();
	}

	~PicExploreWindow() {
	    // Shutdown threads before destroying GUI
	    if (thread_manager_) {
		thread_manager_->shutdown_all();
	    }
	    delete window_;
	}

	void show() {
	    window_->show();
	}

	void run() {

	    // Run the main event loop
	    while (Fl::first_window()) {
		// Wait for and process events with timeout for debug logging
		double wait_result = Fl::wait(1.0); // Wait up to 1 second for events
		if (wait_result < 0) {
		    break;
		}
	    }
	}

	void set_directory_path(const std::string& path) {
	    // Start directory scan using ThreadManager
	    std::cout << "[INFO] Starting directory scan: " << path << std::endl;
	    if (!thread_manager_) {
		fl_alert("Thread manager not initialized");
		return;
	    }
	    bool success = thread_manager_->start_directory_scan(path);
	    if (!success)
		fl_alert("Failed to start directory scan: %s", path.c_str());
	}

    private:

	void create_window() {
	    window_ = new Fl_Window(1200, 800, "PicExplore - Image Gallery and Scanner");

	    // Create menu bar
	    menu_bar_ = new Fl_Menu_Bar(0, 0, 1200, 25);
	    menu_bar_->add("&File/Scan D&irectory...", FL_CTRL + 'i', menu_open_directory_cb, this);
	    menu_bar_->add("&File/Cancel &Scan", FL_CTRL + 'c', menu_cancel_scan_cb, this);
	    menu_bar_->add("&File/Generate &PDF...", FL_CTRL + 'p', menu_generate_pdf_cb, this);
	    menu_bar_->add("&File/&Quit", FL_CTRL + 'q', menu_quit_cb, this);

	    // Create main layout widget
	    layout_widget_ = new Fl_JustifiedLayout(10, 35, 1180, 750);

	    // Connect ThreadManager to layout widget
	    if (layout_widget_ && thread_manager_) {
		layout_widget_->set_thread_manager(thread_manager_.get());
		// Set the layout widget as the UI notification target for redraws
		thread_manager_->set_ui_notify_widget(layout_widget_);
	    }

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

	void setup_thread_callbacks() {
	    if (!thread_manager_) return;

	    // Set up progress callback for the new thread manager
	    thread_manager_->set_progress_callback([this](int current, int total, const std::string& status) {
		// Progress updates from scan thread - forward to layout widget
		if (layout_widget_) {
		    // Use Fl::awake to ensure this runs in the main UI thread
		    Fl::awake([](void* data) {
			auto* window = static_cast<PicExploreWindow*>(data);
			// Update progress display, status bars, etc.
		    }, this);
		}
	    });

	    // Set up metadata callback for incremental image loading
	    thread_manager_->set_metadata_callback([this](const ImageInfo& info) {
		// New image metadata ready - add to layout incrementally
		if (layout_widget_) {
		    // Use Fl::awake to ensure this runs in the main UI thread
		    Fl::awake([](void* data) {
			auto* args = static_cast<std::pair<PicExploreWindow*, ImageInfo>*>(data);
			// Add image to layout widget incrementally
			args->first->layout_widget_->handle_image_info_ready(args->second);
			delete args;
		    }, new std::pair<PicExploreWindow*, ImageInfo>(this, info));
		}
	    });
	}

	// Menu callbacks
	static void menu_open_directory_cb(Fl_Widget*, void* data) {
	    PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
	    const char* path = fl_dir_chooser("Select Directory", ".");
	    if (path) {
		window->set_directory_path(path);
	    }
	}

	static void menu_cancel_scan_cb(Fl_Widget*, void* data) {
	    PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
	    // Use new ThreadManager for cancellation
	    if (window->thread_manager_) {
		window->thread_manager_->cancel_scan();
	    }
	}

	static void menu_generate_pdf_cb(Fl_Widget*, void* data) {
	    PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
	    const char* path = fl_file_chooser("Save PDF As", "*.pdf", "gallery.pdf");
	    if (path) {
		// Generate PDF from current database
		try {
		    // Open database connection
		    DatabaseManager db;
		    std::string db_path = get_cache_db_path();

		    if (!db.open(db_path)) {
			fl_alert("Error: Failed to open database at %s", db_path.c_str());
			return;
		    }

		    // Get all images from database
		    std::vector<ImageInfo> images = db.get_all_images();

		    if (images.empty()) {
			fl_alert("No images found in database. Please scan a directory first.");
			return;
		    }

		    // Create PDF with default options
		    PDFOptions pdf_options;
		    PDFGenerator pdf_gen;
		    Timer timer;
		    StatusReporter reporter(1); // Report every second for GUI

		    reporter.start();
		    reporter.update_status("Generating PDF...");

		    bool success = pdf_gen.generate_pdf(images, path, timer, reporter, pdf_options);

		    reporter.stop();

		    if (success) {
			fl_message("PDF generated successfully: %s", path);
		    } else {
			fl_alert("Error: Failed to generate PDF");
		    }

		} catch (const std::exception& e) {
		    fl_alert("Error generating PDF: %s", e.what());
		}
	    }
	}

	static void menu_quit_cb(Fl_Widget*, void* data) {
	    PicExploreWindow* window = static_cast<PicExploreWindow*>(data);
	    window->window_->hide();
	}

	Fl_Window* window_;
	Fl_Menu_Bar* menu_bar_;
	Fl_JustifiedLayout* layout_widget_;
	std::unique_ptr<ThreadManager> thread_manager_;
};

// Function to run command-line scan only mode
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
	    ("v,verbose", "Enable verbose output")
	    ;

	auto result = options.parse(argc, argv);
	std::vector<std::string> nonopts = result.unmatched();

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

	if (directory.empty() && nonopts.size() > 0) {
	    directory = nonopts[0];
	}

	// Default to current working directory if none specified and we need to scan
	if (directory.empty() && pdf_path.empty()) {
	    directory = std::filesystem::current_path().string();
	    std::cout << "[INFO] No directory specified, using current working directory for scan: "
		      << directory << std::endl;
	}

	// Parse layout padding options
	int layout_pad_default = result["layout-pad"].as<int>();
	int pad_top = result.count("layout-pad-top") ? result["layout-pad-top"].as<int>() : layout_pad_default;
	int pad_bottom = result.count("layout-pad-bottom") ? result["layout-pad-bottom"].as<int>() : layout_pad_default;
	int pad_left = result.count("layout-pad-left") ? result["layout-pad-left"].as<int>() : layout_pad_default;
	int pad_right = result.count("layout-pad-right") ? result["layout-pad-right"].as<int>() : layout_pad_default;

	// Validate arguments - now that we default to CWD, we always have something to work with
	if (directory.empty() && pdf_path.empty()) {
	    std::cerr << "Error: No directory or PDF operation specified" << std::endl;
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
    std::string initial_directory;

    try {
        cxxopts::Options options("picexplore", "Unified image scanner, database manager, and gallery viewer (GUI mode)");
        options.add_options()
            ("h,help", "Show this help message")
            ("i,directory", "Open directory PATH (will scan/build database)", cxxopts::value<std::string>())
        ;

        auto result = options.parse(argc, argv);
        std::vector<std::string> nonopts = result.unmatched();

        if (result.count("help")) {
            std::cout << "PicExplore - Get an Overview of Images in Filesystems\n";
            std::cout << "\nUsage: picexplore [OPTIONS] [path]\n";
            std::cout << options.help() << std::endl;
            std::cout << "\nNew Architecture:\n";
            std::cout << "  * LMDB database is automatically used for all directories\n";
            std::cout << "  * Defaults to current working directory if no directory specified\n";
            std::cout << "  * Unified threading architecture with separate scan/worker/writer threads\n";
            std::cout << "\nExamples:\n";
            std::cout << "  picexplore                           # Launch GUI with current directory\n";
            std::cout << "  picexplore --directory ~/Pictures    # Launch GUI and scan specific directory\n";
            std::cout << "  picexplore --directory ~/Pictures --debug-output /tmp/debug  # With debug output\n";
            std::cout << "  picexplore --scan-only --help        # Show scan-only mode options\n";
            return 0;
        }
        if (result.count("directory")) {
            initial_directory = result["directory"].as<std::string>();
        } else if (!nonopts.empty()) {
            initial_directory = nonopts[0];
        }
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    // Default to current working directory if none specified
    if (initial_directory.empty()) {
        initial_directory = std::filesystem::current_path().string();
        std::cout << "[INFO] No directory specified, using current working directory: "
                  << initial_directory << std::endl;
    }

    // Create and show main window
    PicExploreWindow app;

    // Start scanning the directory
    if (!initial_directory.empty())
        app.set_directory_path(initial_directory);

    app.show();

    std::cout << "PicExplore GUI started." << std::endl;
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

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
