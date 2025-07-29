/*
 * Test incremental layout functionality without GUI
 */

#include <iostream>
#include <filesystem>
#include <vector>
#include "justified_layout.hpp"
#include "simple_svg_1.0.0.hpp"
#include "database.h"

namespace fs = std::filesystem;

// Mock ImageInfo data for testing
std::vector<ImageInfo> create_test_images() {
    std::vector<ImageInfo> images;
    
    // Create test images with different aspect ratios
    std::vector<std::pair<double, bool>> test_data = {
        {2.0, false},    // 2:1 landscape, no thumbnail yet
        {0.5, false},    // 1:2 portrait, no thumbnail yet  
        {1.0, false},    // 1:1 square, no thumbnail yet
        {4.0, false},    // 4:1 wide landscape, no thumbnail yet
        {0.33, false},   // 1:3 tall portrait, no thumbnail yet
    };
    
    for (size_t i = 0; i < test_data.size(); ++i) {
        ImageInfo info;
        info.path = "/tmp/test_images/image" + std::to_string(i) + ".jpg";
        info.hash = "hash" + std::to_string(i);
        info.aspect_ratio = test_data[i].first;
        info.has_thumbnails = test_data[i].second;
        images.push_back(info);
    }
    
    return images;
}

void write_layout_svg(const std::vector<Item>& layout_items, 
                      const std::vector<ImageInfo>& images,
                      double total_height,
                      const std::string& filename,
                      int counter) {
    double layout_width = 1060.0;  // Default width
    
    svg::Document doc(filename, svg::Layout(svg::Dimensions(layout_width, total_height), svg::Layout::BottomLeft));

    // Draw background
    doc << svg::Rectangle(svg::Point(0, 0), layout_width, total_height, svg::Fill(svg::Color(255, 255, 255)));

    // Draw each item
    for (size_t i = 0; i < layout_items.size() && i < images.size(); ++i) {
        const auto& item = layout_items[i];
        const auto& info = images[i];

        // Choose color based on thumbnail availability
        svg::Color fill_color = info.has_thumbnails ? svg::Color(0, 255, 0) : svg::Color(255, 255, 0);  // Green : Yellow
        svg::Color stroke_color = svg::Color(0, 0, 0);  // Black

        // Draw rectangle for this image
        doc << svg::Rectangle(svg::Point(item.l, item.t), item.w, item.h, 
                             svg::Fill(fill_color), svg::Stroke(1, stroke_color));

        // Add text with image info
        if (item.w > 100 && item.h > 30) {  // Only add text if rectangle is large enough
            std::string text = "img" + std::to_string(i) + " " +
                              std::to_string(static_cast<int>(item.w)) + "x" + 
                              std::to_string(static_cast<int>(item.h));
            doc << svg::Text(svg::Point(item.l + 10, item.t + 20), text, 
                           svg::Fill(svg::Color(0, 0, 0)), svg::Font(12, "Arial"));
        }
    }

    doc.save();
    std::cout << "Wrote layout SVG: " << filename << " (update " << counter << ", images: " << images.size() << ")" << std::endl;
}

void test_incremental_layout_updates() {
    std::cout << "Testing incremental layout updates..." << std::endl;
    
    // Create output directory
    std::string debug_dir = "/tmp/layout_debug";
    fs::create_directories(debug_dir);
    
    // Layout configuration
    LayoutCfg config;
    config.w = 1060;
    config.rh = 320;
    config.pt = config.pr = config.pb = config.pl = 10;
    config.sh = config.sv = 5;
    
    // Test incremental updates by adding images one by one
    std::vector<ImageInfo> all_images = create_test_images();
    std::vector<ImageInfo> current_images;
    
    for (size_t i = 0; i < all_images.size(); ++i) {
        // Add one more image
        current_images.push_back(all_images[i]);
        
        // Convert to layout input format
        std::vector<Item> input_items;
        for (const auto& img : current_images) {
            Item item;
            item.ar = img.aspect_ratio;
            input_items.push_back(item);
        }
        
        // Calculate justified layout
        JustifiedLayout layout(input_items, config);
        std::vector<Item> layout_items = layout.boxes();
        double total_height = layout.height();
        
        // Write debug output
        std::string filename = debug_dir + "/incremental_update_" + std::to_string(i) + ".svg";
        write_layout_svg(layout_items, current_images, total_height, filename, i);
        
        std::cout << "  Update " << i << ": " << current_images.size() << " images, height=" << total_height << std::endl;
    }
    
    // Now simulate thumbnails becoming available
    std::cout << "\nSimulating thumbnails becoming available..." << std::endl;
    for (size_t i = 0; i < current_images.size(); ++i) {
        current_images[i].has_thumbnails = true;
        
        // Convert to layout input format (layout doesn't change, but colors will)
        std::vector<Item> input_items;
        for (const auto& img : current_images) {
            Item item;
            item.ar = img.aspect_ratio;
            input_items.push_back(item);
        }
        
        // Calculate justified layout
        JustifiedLayout layout(input_items, config);
        std::vector<Item> layout_items = layout.boxes();
        double total_height = layout.height();
        
        // Write debug output
        std::string filename = debug_dir + "/thumbnail_ready_" + std::to_string(i) + ".svg";
        write_layout_svg(layout_items, current_images, total_height, filename, i + 100);
        
        std::cout << "  Thumbnail " << i << " ready, updated visualization" << std::endl;
    }
    
    std::cout << "\nIncremental layout test completed!" << std::endl;
    std::cout << "Check output files in: " << debug_dir << std::endl;
}

int main(int argc, char* argv[]) {
    test_incremental_layout_updates();
    return 0;
}