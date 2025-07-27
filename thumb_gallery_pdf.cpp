/*
 * thumb_gallery_pdf.cpp - Generate justified-layout PDF image gallery from LMDB thumbnails
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
#include <vector>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <filesystem>
#include <numeric>

// Third-party dependencies
#include "cxxopts.hpp"      // Command line parsing
#include "lmdb.h"          // Lightning Memory-Mapped Database
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"     // Image loading
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h" // Image resizing
#include "pdfimg.hpp"      // PDF output

// Local includes
#include "justified_layout.hpp"

namespace fs = std::filesystem;

// Constants for page layout
const double PAGE_WIDTH_INCHES = 8.5;
const double PAGE_HEIGHT_INCHES = 11.0;
const double PAGE_DPI = 300.0;
const double PAGE_MARGIN_INCHES = 0.5;

const int PAGE_WIDTH_PX = static_cast<int>(PAGE_WIDTH_INCHES * PAGE_DPI);  // 2550
const int PAGE_HEIGHT_PX = static_cast<int>(PAGE_HEIGHT_INCHES * PAGE_DPI); // 3300
const int PAGE_MARGIN_PX = static_cast<int>(PAGE_MARGIN_INCHES * PAGE_DPI); // 150

const int LAYOUT_WIDTH_PX = PAGE_WIDTH_PX - (2 * PAGE_MARGIN_PX);  // 2250
const int LAYOUT_HEIGHT_PX = PAGE_HEIGHT_PX - (2 * PAGE_MARGIN_PX); // 3000

struct ImageInfo {
    std::string path;
    std::string hash;
    double aspect_ratio = 1.0;
    int best_thumb_size = 0;
    std::vector<uint8_t> thumb_data;
    int thumb_width = 0;
    int thumb_height = 0;
};

// Helper to extract hash from LMDB key ending with ":path"
std::string extract_hash_from_key(const char* key, size_t key_size) {
    std::string key_str(key, key_size);
    if (key_str.length() > 5 && key_str.substr(key_str.length() - 5) == ":path") {
        return key_str.substr(0, key_str.length() - 5);
    }
    return "";
}

// Load image data and determine aspect ratio from largest thumbnail
bool load_image_info(MDB_env* env, const std::string& hash, ImageInfo& info) {
    MDB_txn* txn;
    MDB_dbi dbi;
    
    if (mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn) != 0) {
        return false;
    }
    
    if (mdb_dbi_open(txn, nullptr, 0, &dbi) != 0) {
        mdb_txn_abort(txn);
        return false;
    }
    
    // Find largest thumbnail size available
    std::vector<int> sizes = {32, 64, 128, 256, 512, 1024};
    int best_size = 0;
    std::vector<uint8_t> best_data;
    
    for (int size : sizes) {
        std::string thumb_key = hash + ":" + std::to_string(size);
        MDB_val key, data;
        key.mv_data = (void*)thumb_key.c_str();
        key.mv_size = thumb_key.length();
        
        if (mdb_get(txn, dbi, &key, &data) == 0) {
            best_size = size;
            best_data.assign((uint8_t*)data.mv_data, (uint8_t*)data.mv_data + data.mv_size);
        }
    }
    
    mdb_txn_abort(txn);
    
    if (best_size == 0) {
        return false;
    }
    
    // Load thumbnail image to get dimensions and aspect ratio
    int width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(best_data.data(), best_data.size(), 
                                          &width, &height, &channels, 3);
    
    if (!pixels) {
        return false;
    }
    
    info.best_thumb_size = best_size;
    info.thumb_data = std::move(best_data);
    info.thumb_width = width;
    info.thumb_height = height;
    info.aspect_ratio = static_cast<double>(width) / height;
    
    stbi_image_free(pixels);
    return true;
}

// Resize image data to fit within target dimensions, maintaining aspect ratio
std::vector<uint8_t> resize_image_to_fit(const std::vector<uint8_t>& image_data,
                                        int src_width, int src_height,
                                        int target_width, int target_height) {
    // Load original image
    int width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(image_data.data(), image_data.size(),
                                          &width, &height, &channels, 3);
    if (!pixels) {
        return {};
    }
    
    // Calculate scale to fit within target bounds
    double scale_x = static_cast<double>(target_width) / width;
    double scale_y = static_cast<double>(target_height) / height;
    double scale = std::min(scale_x, scale_y);
    
    int new_width = static_cast<int>(width * scale);
    int new_height = static_cast<int>(height * scale);
    
    // Resize image
    std::vector<uint8_t> resized(new_width * new_height * 3);
    if (!stbir_resize_uint8_linear(pixels, width, height, 0,
                                  resized.data(), new_width, new_height, 0, STBIR_RGB)) {
        stbi_image_free(pixels);
        return {};
    }
    
    stbi_image_free(pixels);
    return resized;
}

// Composite image into page buffer at specified position
void composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
                    const std::vector<uint8_t>& image_data, int img_width, int img_height,
                    int x, int y, int box_width, int box_height) {
    // Center image within the box
    int offset_x = x + (box_width - img_width) / 2;
    int offset_y = y + (box_height - img_height) / 2;
    
    for (int row = 0; row < img_height; row++) {
        int page_y = offset_y + row;
        if (page_y < 0 || page_y >= page_height) continue;
        
        int page_row_start = page_y * page_width * 3;
        int img_row_start = row * img_width * 3;
        
        for (int col = 0; col < img_width; col++) {
            int page_x = offset_x + col;
            if (page_x < 0 || page_x >= page_width) continue;
            
            int page_pixel = page_row_start + page_x * 3;
            int img_pixel = img_row_start + col * 3;
            
            page_buffer[page_pixel] = image_data[img_pixel];         // R
            page_buffer[page_pixel + 1] = image_data[img_pixel + 1]; // G
            page_buffer[page_pixel + 2] = image_data[img_pixel + 2]; // B
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("thumb_gallery_pdf", "Generate justified-layout PDF image gallery from LMDB thumbnails");
        
        options.add_options()
            ("h,help", "Print usage")
            ("lmdb", "Input LMDB database path", cxxopts::value<std::string>())
            ("o,output", "Output PDF file path", cxxopts::value<std::string>())
            ("row-height", "Target row height in pixels", cxxopts::value<int>()->default_value("150"))
            ("margin", "Layout margin in pixels", cxxopts::value<int>()->default_value("10"));

        auto result = options.parse(argc, argv);

        if (result.count("help") || !result.count("lmdb") || !result.count("output")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        std::string lmdb_path = result["lmdb"].as<std::string>();
        std::string output_path = result["output"].as<std::string>();
        int row_height = result["row-height"].as<int>();
        int margin = result["margin"].as<int>();

        // Open LMDB database
        MDB_env* env;
        if (mdb_env_create(&env) != 0) {
            std::cerr << "Error: Failed to create LMDB environment" << std::endl;
            return 1;
        }

        if (mdb_env_open(env, lmdb_path.c_str(), MDB_RDONLY, 0664) != 0) {
            std::cerr << "Error: Failed to open LMDB database: " << lmdb_path << std::endl;
            mdb_env_close(env);
            return 1;
        }

        // Scan for image paths
        std::vector<ImageInfo> images;
        MDB_txn* txn;
        MDB_dbi dbi;
        MDB_cursor* cursor;

        if (mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn) != 0) {
            std::cerr << "Error: Failed to begin LMDB transaction" << std::endl;
            mdb_env_close(env);
            return 1;
        }

        if (mdb_dbi_open(txn, nullptr, 0, &dbi) != 0) {
            std::cerr << "Error: Failed to open LMDB database" << std::endl;
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return 1;
        }

        if (mdb_cursor_open(txn, dbi, &cursor) != 0) {
            std::cerr << "Error: Failed to open LMDB cursor" << std::endl;
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return 1;
        }

        MDB_val key, data;
        while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
            std::string hash = extract_hash_from_key((char*)key.mv_data, key.mv_size);
            if (!hash.empty()) {
                ImageInfo info;
                info.hash = hash;
                info.path = std::string((char*)data.mv_data, data.mv_size);
                
                if (load_image_info(env, hash, info)) {
                    images.push_back(std::move(info));
                }
            }
        }

        mdb_cursor_close(cursor);
        mdb_txn_abort(txn);

        if (images.empty()) {
            std::cerr << "No images found in database" << std::endl;
            mdb_env_close(env);
            return 1;
        }

        // Sort images alphabetically by path
        std::sort(images.begin(), images.end(), 
                 [](const ImageInfo& a, const ImageInfo& b) {
                     return a.path < b.path;
                 });

        std::cout << "Found " << images.size() << " images" << std::endl;

        // Setup layout configuration
        LayoutCfg layout_cfg;
        layout_cfg.w = LAYOUT_WIDTH_PX;  // Available width
        layout_cfg.pl = layout_cfg.pr = layout_cfg.pt = layout_cfg.pb = 0; // No padding (we handle margins ourselves)
        layout_cfg.sh = layout_cfg.sv = margin; // Spacing between images
        layout_cfg.rh = row_height; // Target row height
        layout_cfg.tol = 0.25; // ±25% tolerance
        layout_cfg.widows = true;
        layout_cfg.ws = WidowStyle::Left;

        // Convert images to layout items
        std::vector<Item> items;
        for (const auto& img : images) {
            Item item;
            item.ar = img.aspect_ratio;
            items.push_back(item);
        }

        // Calculate pages needed
        std::vector<std::vector<Item>> pages;
        size_t current_item = 0;

        while (current_item < items.size()) {
            // Try to fit as many rows as possible on this page
            std::vector<Item> page_items;
            double current_y = 0;

            while (current_y < LAYOUT_HEIGHT_PX && current_item < items.size()) {
                // Calculate a subset that fits in remaining space
                std::vector<Item> remaining_items(items.begin() + current_item, items.end());
                
                // Temporarily set max rows to prevent overflow
                LayoutCfg temp_cfg = layout_cfg;
                temp_cfg.maxRows = std::max(1, static_cast<int>((LAYOUT_HEIGHT_PX - current_y) / row_height));
                
                JustifiedLayout layout(remaining_items, temp_cfg);
                
                if (layout.boxes().empty()) break;
                
                // Check if layout fits in remaining space
                if (current_y + layout.height() <= LAYOUT_HEIGHT_PX) {
                    // All boxes fit, adjust positions
                    for (auto box : layout.boxes()) {
                        box.t += current_y;
                        page_items.push_back(box);
                    }
                    current_item += layout.boxes().size();
                    current_y += layout.height();
                } else {
                    // Only partial fit - break and start new page
                    if (page_items.empty()) {
                        // Force at least one row even if it overflows
                        temp_cfg.maxRows = 1;
                        JustifiedLayout single_row_layout(remaining_items, temp_cfg);
                        for (auto box : single_row_layout.boxes()) {
                            box.t += current_y;
                            page_items.push_back(box);
                        }
                        current_item += single_row_layout.boxes().size();
                    }
                    break;
                }
            }

            if (!page_items.empty()) {
                pages.push_back(page_items);
            }
        }

        std::cout << "Layout calculated: " << pages.size() << " pages" << std::endl;

        // Generate PDF
        pdfimg::PDFDocument pdf;
        size_t global_item_idx = 0;  // Track global item index across all pages

        for (size_t page_idx = 0; page_idx < pages.size(); page_idx++) {
            std::cout << "Rendering page " << (page_idx + 1) << " with " << pages[page_idx].size() << " images" << std::endl;

            // Create page buffer (white background)
            std::vector<uint8_t> page_buffer(PAGE_WIDTH_PX * PAGE_HEIGHT_PX * 3, 255);

            // Render images on this page
            for (size_t item_idx = 0; item_idx < pages[page_idx].size(); item_idx++) {
                const Item& box = pages[page_idx][item_idx];
                const ImageInfo& img = images[global_item_idx + item_idx];

                // Calculate actual position with page margins
                int x = PAGE_MARGIN_PX + static_cast<int>(box.l);
                int y = PAGE_MARGIN_PX + static_cast<int>(box.t);
                int w = static_cast<int>(box.w);
                int h = static_cast<int>(box.h);

                // Resize image to fit the box
                std::vector<uint8_t> resized = resize_image_to_fit(img.thumb_data, 
                                                                 img.thumb_width, img.thumb_height,
                                                                 w, h);
                if (!resized.empty()) {
                    // Get resized dimensions
                    double scale = std::min(static_cast<double>(w) / img.thumb_width,
                                          static_cast<double>(h) / img.thumb_height);
                    int resized_w = static_cast<int>(img.thumb_width * scale);
                    int resized_h = static_cast<int>(img.thumb_height * scale);

                    composite_image(page_buffer, PAGE_WIDTH_PX, PAGE_HEIGHT_PX,
                                  resized, resized_w, resized_h, x, y, w, h);
                }
            }

            // Add page to PDF
            pdf.add_image_page(page_buffer.data(), PAGE_WIDTH_PX, PAGE_HEIGHT_PX,
                             PAGE_WIDTH_PX * 3, true, pdfimg::CompressionType::None, PAGE_DPI);
            
            global_item_idx += pages[page_idx].size();
        }

        // Write PDF file
        if (!pdf.save(output_path)) {
            std::cerr << "Error: Failed to write PDF file: " << output_path << std::endl;
            mdb_env_close(env);
            return 1;
        }

        std::cout << "Successfully generated PDF: " << output_path << std::endl;
        mdb_env_close(env);
        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}