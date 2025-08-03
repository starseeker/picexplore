/*
 * picexplore_mvc.cpp - MVC-refactored main application for picexplore
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
#include <memory>

// MVC Components
#include "controllers.hpp"
#include "views.hpp"
#include "fltk_views.hpp"

// Core functionality
#include "database.hpp"
#include "pdf.hpp"
#include "utils.hpp"

// Command line parsing
#include "cxxopts.hpp"

// GUI includes
#include <FL/Fl.H>

namespace fs = std::filesystem;

/**
 * Application class that coordinates the MVC architecture
 */
class PicExploreApplication {
public:
    PicExploreApplication() : initialized_(false) {}
    
    ~PicExploreApplication() {
        shutdown();
    }
    
    bool initialize() {
        if (initialized_) {
            return true;
        }
        
        // Initialize FLTK threading
        Fl::lock();
        
        // Create view factory
        view_factory_ = std::make_unique<FLTKViewFactory>();
        
        // Create application controller
        app_controller_ = std::make_shared<ApplicationController>();
        if (!app_controller_->initialize()) {
            std::cerr << "Failed to initialize application controller" << std::endl;
            return false;
        }
        
        // Create main view
        main_view_ = view_factory_->create_main_view();
        if (!main_view_ || !main_view_->initialize()) {
            std::cerr << "Failed to initialize main view" << std::endl;
            return false;
        }
        
        // Connect controller and view
        main_view_->set_controller(app_controller_);
        app_controller_->set_main_view(main_view_.get());
        
        initialized_ = true;
        return true;
    }
    
    void shutdown() {
        if (app_controller_) {
            app_controller_->shutdown();
        }
        initialized_ = false;
    }
    
    int run_gui_mode(int argc, char* argv[]) {
        if (!initialize()) {
            return 1;
        }
        
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
                std::cout << "PicExplore - Get an Overview of Images in Filesystems (MVC Architecture)\n";
                std::cout << "\nUsage: picexplore [OPTIONS] [path]\n";
                std::cout << options.help() << std::endl;
                std::cout << "\nNew MVC Architecture Features:\n";
                std::cout << "  * Separated model, view, and controller layers\n";
                std::cout << "  * Modular UI components with clear interfaces\n";
                std::cout << "  * Centralized state management through StateStore\n";
                std::cout << "  * Event-driven communication between components\n";
                std::cout << "\nExamples:\n";
                std::cout << "  picexplore                           # Launch GUI with current directory\n";
                std::cout << "  picexplore --directory ~/Pictures    # Launch GUI and scan specific directory\n";
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

        // Show main window
        main_view_->show();

        // Start scanning the directory if specified
        if (!initial_directory.empty()) {
            app_controller_->open_directory(initial_directory);
        }

        std::cout << "PicExplore GUI started with MVC architecture." << std::endl;
        std::cout << "Click thumbnails to select, use mouse wheel to scroll." << std::endl;

        // Run the main event loop
        main_view_->run();

        return 0;
    }
    
    int run_scan_only_mode(int argc, char* argv[]) {
        // For scan-only mode, we can still use the controller layer without the view
        if (!app_controller_) {
            app_controller_ = std::make_shared<ApplicationController>();
            if (!app_controller_->initialize()) {
                std::cerr << "Failed to initialize application controller" << std::endl;
                return 1;
            }
        }
        
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
                std::cout << "  # Scan directory and build/update database (no GUI):\n";
                std::cout << "  picexplore --scan-only --directory /path/to/photos\n\n";
                std::cout << "  # Generate PDF from existing database (no GUI):\n";
                std::cout << "  picexplore --scan-only --pdf gallery.pdf\n\n";
                std::cout << "  # Scan directory and generate PDF in one step (no GUI):\n";
                std::cout << "  picexplore --scan-only --directory /path/to/photos --pdf gallery.pdf\n\n";
                return 0;
            }

            std::string directory = result.count("directory") ? result["directory"].as<std::string>() : "";
            std::string pdf_path = result.count("pdf") ? result["pdf"].as<std::string>() : "";
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

            // Validate arguments
            if (directory.empty() && pdf_path.empty()) {
                std::cerr << "Error: No directory or PDF operation specified" << std::endl;
                return 1;
            }

            // Check if directory exists
            if (!directory.empty() && !fs::exists(directory)) {
                std::cerr << "Error: Directory does not exist: " << directory << std::endl;
                return 1;
            }

            std::cout << "PicExplore - Unified Image Scanner, Database Manager, and Gallery Viewer (MVC Architecture)" << std::endl;

            bool scan_needed = !directory.empty();
            bool pdf_needed = !pdf_path.empty();

            // Phase 1: Directory scanning (if requested)
            if (scan_needed) {
                std::cout << "Scanning directory: " << directory << std::endl;
                
                bool scan_success = false;
                int images_processed = 0;
                
                // Use controller for scanning
                auto scan_controller = app_controller_->get_scan_controller();
                scan_success = scan_controller->start_directory_scan(
                    directory,
                    [&](const ScanResult& result) {
                        scan_success = result.success;
                        images_processed = result.images_processed;
                        if (!result.success) {
                            std::cerr << "Scan failed: " << result.error_message << std::endl;
                        }
                    },
                    [&](int current, int total, const std::string& status) {
                        if (verbose) {
                            std::cout << "Progress: " << current << "/" << total << " - " << status << std::endl;
                        }
                    }
                );
                
                if (!scan_success) {
                    std::cerr << "Error: Failed to start directory scan" << std::endl;
                    return 1;
                }
                
                // Wait for scan to complete
                while (scan_controller->is_scanning()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                
                std::cout << "Processed " << images_processed << " images" << std::endl;
            }

            // Phase 2: PDF generation (if requested)
            if (pdf_needed) {
                std::cout << "Generating PDF: " << pdf_path << std::endl;
                
                PDFGenerationConfig config;
                config.output_path = pdf_path;
                config.row_height = row_height;
                config.margin = margin;
                config.layout_pad_top = pad_top;
                config.layout_pad_bottom = pad_bottom;
                config.layout_pad_left = pad_left;
                config.layout_pad_right = pad_right;
                
                bool pdf_success = false;
                std::string pdf_message;
                
                app_controller_->generate_pdf(config, [&](bool success, const std::string& message) {
                    pdf_success = success;
                    pdf_message = message;
                });
                
                if (pdf_success) {
                    std::cout << pdf_message << std::endl;
                } else {
                    std::cerr << "Error: " << pdf_message << std::endl;
                    return 1;
                }
            }

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

private:
    bool initialized_;
    std::unique_ptr<ViewFactory> view_factory_;
    std::shared_ptr<ApplicationController> app_controller_;
    std::shared_ptr<PicExploreView> main_view_;
};

int main(int argc, char* argv[]) {
    PicExploreApplication app;
    
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
        return app.run_scan_only_mode(argc, argv);
    } else {
        return app.run_gui_mode(argc, argv);
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