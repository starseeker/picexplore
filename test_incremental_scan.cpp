/*
 * Test the incremental layout updates during real image scanning
 */

#include <iostream>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include "database.h"

namespace fs = std::filesystem;

class IncrementalTestCallback {
public:
    IncrementalTestCallback(const std::string& debug_dir) 
        : debug_dir_(debug_dir), update_counter_(0) {
        fs::create_directories(debug_dir_);
    }
    
    void operator()(const ImageInfo& info) {
        std::cout << "Metadata ready for: " << info.path 
                  << " (aspect ratio: " << info.aspect_ratio 
                  << ", has_thumbnails: " << info.has_thumbnails << ")" << std::endl;
        
        // Add to our list
        images_.push_back(info);
        
        // Write debug output showing incremental addition
        write_debug_svg();
        
        update_counter_++;
    }
    
private:
    void write_debug_svg() {
        // Create a simple SVG showing the current state
        std::string filename = debug_dir_ + "/scan_update_" + std::to_string(update_counter_) + ".svg";
        
        std::ofstream file(filename);
        file << "<?xml version=\"1.0\" standalone=\"no\" ?>\n";
        file << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
        file << "<svg width=\"400px\" height=\"" << (images_.size() * 50 + 20) << "px\" xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" >\n";
        
        // Background
        file << "\t<rect x=\"0\" y=\"0\" width=\"400\" height=\"" << (images_.size() * 50 + 20) << "\" fill=\"rgb(240,240,240)\" />\n";
        
        // Draw a bar for each image
        for (size_t i = 0; i < images_.size(); ++i) {
            const auto& img = images_[i];
            int y = 10 + i * 50;
            int width = static_cast<int>(img.aspect_ratio * 100);
            width = std::min(300, std::max(20, width));  // Clamp width
            
            std::string color = img.has_thumbnails ? "rgb(0,255,0)" : "rgb(255,255,0)";
            
            file << "\t<rect x=\"10\" y=\"" << y << "\" width=\"" << width << "\" height=\"40\" fill=\"" << color << "\" stroke=\"rgb(0,0,0)\" />\n";
            file << "\t<text x=\"15\" y=\"" << (y + 25) << "\" fill=\"rgb(0,0,0)\" font-size=\"10\" font-family=\"Arial\">" 
                 << "img" << i << " AR:" << std::fixed << std::setprecision(2) << img.aspect_ratio << "</text>\n";
        }
        
        file << "</svg>\n";
        file.close();
        
        std::cout << "  Debug SVG written: " << filename << " (" << images_.size() << " images)" << std::endl;
    }
    
    std::string debug_dir_;
    std::vector<ImageInfo> images_;
    int update_counter_;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image_directory> [debug_output_dir]" << std::endl;
        return 1;
    }
    
    std::string image_dir = argv[1];
    std::string debug_dir = (argc > 2) ? argv[2] : "/tmp/scan_debug";
    
    std::cout << "Testing incremental scanning with debug output..." << std::endl;
    std::cout << "Image directory: " << image_dir << std::endl;
    std::cout << "Debug output: " << debug_dir << std::endl;
    
    // Create callback to capture incremental updates
    IncrementalTestCallback callback(debug_dir);
    
    // Create database and set callback
    DatabaseManager db;
    db.set_image_info_callback(callback);
    
    // Open a temporary database
    std::string db_path = "/tmp/test_scan.db";
    if (!db.open(db_path)) {
        std::cerr << "Failed to open database: " << db_path << std::endl;
        return 1;
    }
    
    // Scan the directory - this should trigger incremental callbacks
    Timer timer;
    StatusReporter reporter(1);
    reporter.start();
    
    int processed = db.scan_directory_parallel(image_dir, timer, reporter);
    
    reporter.stop();
    
    std::cout << "\nScanning completed!" << std::endl;
    std::cout << "Processed: " << processed << " images" << std::endl;
    std::cout << "Check debug output in: " << debug_dir << std::endl;
    
    return 0;
}