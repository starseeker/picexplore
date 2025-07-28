/*
 * database.h - LMDB database and thumbnail operations for picscan
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

#include <string>
#include <vector>
#include <memory>
#include "lmdb.h"
#include "utils.h"

// Image information structure
struct ImageInfo {
    std::string path;
    std::string hash;
    double aspect_ratio = 1.0;
    int best_thumb_size = 0;
    std::vector<uint8_t> thumb_data;
    int thumb_width = 0;
    int thumb_height = 0;
};

// Database manager class
class DatabaseManager {
public:
    DatabaseManager();
    ~DatabaseManager();
    
    bool open(const std::string& db_path);
    void close();
    
    // Scanning and thumbnail generation
    int scan_directory(const std::string& directory, Timer& timer, StatusReporter& reporter);
    
    // Database querying
    std::vector<ImageInfo> get_all_images();
    bool has_thumbnails(const std::string& hash);
    
private:
    MDB_env* env_;
    MDB_txn* txn_;
    MDB_dbi dbi_;
    bool is_open_;
    
    // Thumbnail generation
    bool generate_thumbnails(const std::string& filepath, const std::string& hash, 
                           unsigned char* image_data, int width, int height, int channels);
    std::vector<uint8_t> decode_jpeg_thumbnail_rgb(const std::string& filepath, int scale_factor, 
                                                  int* actual_width, int* actual_height);
    int calculate_scale_factor(int image_width, int image_height, int target_width, int target_height);
    
    // Database operations
    bool begin_transaction();
    bool commit_transaction();
    void abort_transaction();
    bool store_key_value(const std::string& key, const std::string& value);
    bool store_key_data(const std::string& key, const std::vector<uint8_t>& data);
    bool get_key_value(const std::string& key, std::string& value);
    bool get_key_data(const std::string& key, std::vector<uint8_t>& data);
    std::string extract_hash_from_key(const char* key, size_t key_size);
    bool load_image_info(const std::string& hash, ImageInfo& info);
};