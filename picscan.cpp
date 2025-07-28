/*
 * picscan.cpp - Unified image scanner and PDF gallery generator
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

#include "cxxopts.hpp"
#include "database.h"
#include "pdf.h"
#include "utils.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("picscan", "Unified image scanner and PDF gallery generator");
        
        options.add_options()
            ("h,help", "Print usage")
            ("d,directory", "Directory to scan for images", cxxopts::value<std::string>())
            ("db,database", "LMDB database path", cxxopts::value<std::string>()->default_value("./images.db"))
            ("pdf,output", "PDF output file path", cxxopts::value<std::string>())
            ("row-height", "Target row height in pixels for PDF layout", cxxopts::value<int>()->default_value("150"))
            ("margin", "Layout margin in pixels for PDF", cxxopts::value<int>()->default_value("10"))
            ("v,verbose", "Enable verbose output")
        ;

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::cout << "\nExamples:\n";
            std::cout << "  # Scan directory and build/update database:\n";
            std::cout << "  picscan --directory /path/to/photos\n\n";
            std::cout << "  # Generate PDF from existing database:\n"; 
            std::cout << "  picscan --pdf gallery.pdf\n\n";
            std::cout << "  # Scan directory and generate PDF in one step:\n";
            std::cout << "  picscan --directory /path/to/photos --pdf gallery.pdf\n\n";
            return 0;
        }

        std::string directory = result.count("directory") ? result["directory"].as<std::string>() : "";
        std::string db_path = result["database"].as<std::string>();
        std::string pdf_path = result.count("pdf") ? result["pdf"].as<std::string>() : "";
        int row_height = result["row-height"].as<int>();
        int margin = result["margin"].as<int>();
        bool verbose = result.count("verbose") > 0;

        // Validate arguments
        if (directory.empty() && pdf_path.empty()) {
            std::cerr << "Error: Must specify either --directory or --pdf (or both)" << std::endl;
            return 1;
        }

        // Check if directory exists
        if (!directory.empty() && !fs::exists(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            return 1;
        }

        std::cout << "PicScan - Unified Image Scanner and PDF Gallery Generator" << std::endl;
        
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
            
            PDFGenerator pdf_gen;
            if (!pdf_gen.generate_pdf(images, pdf_path, timer, reporter, row_height, margin)) {
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