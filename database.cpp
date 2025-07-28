/*
 * database.cpp - LMDB database and thumbnail operations for picscan
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

#include "database.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>

// Third-party dependencies
#include "xxhash.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "picojpeg.h"

namespace fs = std::filesystem;

// Helper structure for file data callback for picojpeg
struct FileData {
    std::vector<uint8_t> data;
    size_t offset;
};

// Callback function for picojpeg to read file data  
unsigned char pjpeg_need_bytes_callback(unsigned char* pBuf, unsigned char buf_size, unsigned char *pBytes_actually_read, void *pCallback_data) {
    FileData* file_data = static_cast<FileData*>(pCallback_data);
    
    size_t bytes_available = file_data->data.size() - file_data->offset;
    size_t bytes_to_read = std::min((size_t)buf_size, bytes_available);
    
    if (bytes_to_read > 0) {
        std::memcpy(pBuf, file_data->data.data() + file_data->offset, bytes_to_read);
        file_data->offset += bytes_to_read;
    }
    
    *pBytes_actually_read = (unsigned char)bytes_to_read;
    return 0;
}

DatabaseManager::DatabaseManager() : env_(nullptr), txn_(nullptr), dbi_(0), is_open_(false) {
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open(const std::string& db_path) {
    if (is_open_) {
        close();
    }
    
    int rc = mdb_env_create(&env_);
    if (rc != 0) {
        return false;
    }
    
    // Set map size to handle large databases (1GB)
    rc = mdb_env_set_mapsize(env_, 1024 * 1024 * 1024);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    rc = mdb_env_open(env_, db_path.c_str(), MDB_NOSUBDIR, 0664);
    if (rc != 0) {
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    is_open_ = true;
    return true;
}

void DatabaseManager::close() {
    if (txn_) {
        mdb_txn_abort(txn_);
        txn_ = nullptr;
    }
    if (env_) {
        mdb_env_close(env_);
        env_ = nullptr;
    }
    is_open_ = false;
}

bool DatabaseManager::begin_transaction() {
    if (!is_open_ || txn_) return false;
    
    int rc = mdb_txn_begin(env_, nullptr, 0, &txn_);
    if (rc != 0) {
        return false;
    }
    
    rc = mdb_dbi_open(txn_, nullptr, MDB_CREATE, &dbi_);
    if (rc != 0) {
        mdb_txn_abort(txn_);
        txn_ = nullptr;
        return false;
    }
    
    return true;
}

bool DatabaseManager::commit_transaction() {
    if (!txn_) return false;
    
    int rc = mdb_txn_commit(txn_);
    txn_ = nullptr;
    return (rc == 0);
}

void DatabaseManager::abort_transaction() {
    if (txn_) {
        mdb_txn_abort(txn_);
        txn_ = nullptr;
    }
}

bool DatabaseManager::store_key_value(const std::string& key, const std::string& value) {
    if (!txn_) return false;
    
    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    v.mv_data = (void*)value.c_str();
    v.mv_size = value.length();
    
    return mdb_put(txn_, dbi_, &k, &v, 0) == 0;
}

bool DatabaseManager::store_key_data(const std::string& key, const std::vector<uint8_t>& data) {
    if (!txn_) return false;
    
    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    v.mv_data = (void*)data.data();
    v.mv_size = data.size();
    
    return mdb_put(txn_, dbi_, &k, &v, 0) == 0;
}

bool DatabaseManager::get_key_value(const std::string& key, std::string& value) {
    if (!txn_) return false;
    
    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    
    if (mdb_get(txn_, dbi_, &k, &v) == 0) {
        value.assign((char*)v.mv_data, v.mv_size);
        return true;
    }
    return false;
}

bool DatabaseManager::get_key_data(const std::string& key, std::vector<uint8_t>& data) {
    if (!txn_) return false;
    
    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    
    if (mdb_get(txn_, dbi_, &k, &v) == 0) {
        data.assign((uint8_t*)v.mv_data, (uint8_t*)v.mv_data + v.mv_size);
        return true;
    }
    return false;
}

int DatabaseManager::calculate_scale_factor(int image_width, int image_height, int target_width, int target_height) {
    int scale_x = image_width / target_width;
    int scale_y = image_height / target_height;
    int scale_factor = std::max(scale_x, scale_y);
    
    // Round to nearest valid scale factor (1, 2, 4, 8)
    if (scale_factor <= 1) return 1;
    else if (scale_factor <= 2) return 2;
    else if (scale_factor <= 4) return 4;
    else return 8;
}

std::vector<uint8_t> DatabaseManager::decode_jpeg_thumbnail_rgb(const std::string& filepath, int scale_factor, int* actual_width, int* actual_height) {
    // Load file into memory
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        fprintf(stderr, "Error: Failed to open file for JPEG decoding: '%s'\n", filepath.c_str());
        return {};
    }
    
    FileData file_data;
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    file_data.data.resize(file_size);
    file.read(reinterpret_cast<char*>(file_data.data.data()), file_size);
    file_data.offset = 0;
    
    // Initialize picojpeg with the specified scale factor
    pjpeg_image_info_t image_info;
    unsigned char status = pjpeg_decode_init_scale(&image_info, pjpeg_need_bytes_callback, &file_data, scale_factor);
    if (status != 0) {
        fprintf(stderr, "Error: PicoJPEG decode init failed for '%s' (status: %d)\n", filepath.c_str(), status);
        return {};
    }
    
    // Calculate output dimensions
    int block_size = 8 / scale_factor;
    if (block_size < 1) block_size = 1;
    
    int output_width = image_info.m_MCUSPerRow * block_size;
    int output_height = image_info.m_MCUSPerCol * block_size;
    
    if (actual_width) *actual_width = output_width;
    if (actual_height) *actual_height = output_height;
    
    // Allocate RGB output buffer
    std::vector<uint8_t> rgb_data(output_width * output_height * 3);
    
    // Decode MCUs
    for (int mcu_y = 0; mcu_y < image_info.m_MCUSPerCol; mcu_y++) {
        for (int mcu_x = 0; mcu_x < image_info.m_MCUSPerRow; mcu_x++) {
            status = pjpeg_decode_mcu();
            if (status != 0) {
                if (status == PJPG_NO_MORE_BLOCKS) {
                    break;
                }
                fprintf(stderr, "Error: PicoJPEG MCU decode failed for '%s' (status: %d)\n", filepath.c_str(), status);
                return {};
            }
            
            // Copy MCU data to output RGB buffer
            int dst_x = mcu_x * block_size;
            int dst_y = mcu_y * block_size;
            
            for (int by = 0; by < block_size; by++) {
                for (int bx = 0; bx < block_size; bx++) {
                    int src_idx = by * 8 + bx; // Source is always 8-pixel stride
                    int dst_idx = ((dst_y + by) * output_width + (dst_x + bx)) * 3;
                    
                    if (dst_idx + 2 < rgb_data.size()) {
                        if (image_info.m_comps == 1) {
                            // Grayscale
                            unsigned char y = image_info.m_pMCUBufR[src_idx];
                            rgb_data[dst_idx + 0] = y;
                            rgb_data[dst_idx + 1] = y;
                            rgb_data[dst_idx + 2] = y;
                        } else {
                            // Color
                            rgb_data[dst_idx + 0] = image_info.m_pMCUBufR[src_idx];
                            rgb_data[dst_idx + 1] = image_info.m_pMCUBufG[src_idx];
                            rgb_data[dst_idx + 2] = image_info.m_pMCUBufB[src_idx];
                        }
                    }
                }
            }
        }
    }
    
    return rgb_data;
}

bool DatabaseManager::generate_thumbnails(const std::string& filepath, const std::string& hash,
                                         unsigned char* image_data, int width, int height, int channels) {
    // Thumbnail sizes to generate (must match what PDF generator expects)
    std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024};
    
    auto ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    
    bool thumbnails_generated = true;
    
    if (ext == ".jpg" || ext == ".jpeg") {
        // For JPEG files, use picojpeg with optimized scale factor grouping
        std::map<int, std::vector<int>> scale_groups; // scale_factor -> list of thumb_sizes
        
        // Group thumbnail sizes by their optimal scale factor
        for (int thumb_size : thumb_sizes) {
            // Calculate thumbnail dimensions maintaining aspect ratio
            double aspect_ratio = (double)width / height;
            int thumb_width, thumb_height;
            
            if (width > height) {
                thumb_width = thumb_size;
                thumb_height = (int)(thumb_size / aspect_ratio);  
            } else {
                thumb_height = thumb_size;
                thumb_width = (int)(thumb_size * aspect_ratio);
            }
            
            // Skip if image is already smaller than thumbnail size
            if (width <= thumb_width && height <= thumb_height) {
                continue;
            }
            
            int scale_factor = calculate_scale_factor(width, height, thumb_width, thumb_height);
            scale_groups[scale_factor].push_back(thumb_size);
        }
        
        // Process each scale factor group
        for (const auto& group : scale_groups) {
            int scale_factor = group.first;
            const std::vector<int>& sizes = group.second;
            
            // Decode once for this scale factor
            int actual_width, actual_height;
            std::vector<uint8_t> rgb_thumb = decode_jpeg_thumbnail_rgb(filepath, scale_factor, &actual_width, &actual_height);
            
            if (!rgb_thumb.empty()) {
                // Use this RGB data for all thumbnail sizes in this group
                for (int thumb_size : sizes) {
                    std::vector<uint8_t> thumb_data = encode_jpeg(rgb_thumb.data(), actual_width, actual_height, 90);
                    
                    if (!thumb_data.empty()) {
                        std::string thumb_key = hash + ":" + std::to_string(thumb_size);
                        if (!store_key_data(thumb_key, thumb_data)) {
                            fprintf(stderr, "Error: Failed to store thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
                            thumbnails_generated = false;
                        }
                    } else {
                        fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
                        thumbnails_generated = false;
                    }
                }
            } else {
                // Failed to decode with picojpeg, fall back to stb_image for these sizes  
                fprintf(stderr, "Warning: JPEG decoding failed for '%s' (scale factor %d), falling back to STB\n", filepath.c_str(), scale_factor);
                thumbnails_generated = false;
                break;
            }
        }
    } else {
        // For non-JPEG files, set flag to use fallback processing
        thumbnails_generated = false;
    }
    
    // Fallback processing for non-JPEG files or if picojpeg failed
    if (!thumbnails_generated) {
        thumbnails_generated = true; // Reset the flag
        for (int thumb_size : thumb_sizes) {
            // Calculate thumbnail dimensions maintaining aspect ratio
            double aspect_ratio = (double)width / height;
            int thumb_width, thumb_height;
            
            if (width > height) {
                thumb_width = thumb_size;
                thumb_height = (int)(thumb_size / aspect_ratio);
            } else {
                thumb_height = thumb_size;
                thumb_width = (int)(thumb_size * aspect_ratio);
            }
            
            // Skip if image is already smaller than thumbnail size
            if (width <= thumb_width && height <= thumb_height) {
                continue;
            }
            
            // Use stb_image_resize for non-JPEG or fallback
            std::vector<uint8_t> thumb_data;
            bool thumb_success = false;
            
            // Convert to RGB if needed
            unsigned char* rgb_data = nullptr;
            bool need_free_rgb = false;
            
            if (channels == 3) {
                rgb_data = image_data;
            } else {
                rgb_data = (unsigned char*)malloc(width * height * 3);
                need_free_rgb = true;
                
                for (int i = 0; i < width * height; i++) {
                    if (channels == 1) {
                        // Grayscale to RGB
                        rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i];
                    } else if (channels == 2) {
                        // Grayscale + Alpha to RGB (ignore alpha)
                        rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i*2];
                    } else if (channels == 4) {
                        // RGBA to RGB (ignore alpha)
                        rgb_data[i*3] = image_data[i*4];
                        rgb_data[i*3+1] = image_data[i*4+1];
                        rgb_data[i*3+2] = image_data[i*4+2];
                    }
                }
            }
            
            // Resize image
            std::vector<uint8_t> resized_rgb(thumb_width * thumb_height * 3);
            if (stbir_resize_uint8_linear(rgb_data, width, height, 0,
                                        resized_rgb.data(), thumb_width, thumb_height, 0, STBIR_RGB)) {
                
                // Encode as JPEG
                thumb_data = encode_jpeg(resized_rgb.data(), thumb_width, thumb_height, 90);
                if (!thumb_data.empty()) {
                    thumb_success = true;
                } else {
                    fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
                }
            } else {
                fprintf(stderr, "Error: Failed to resize image for '%s' (size %d)\n", filepath.c_str(), thumb_size);
            }
            
            if (need_free_rgb) {
                free(rgb_data);
            }
            
            if (thumb_success && !thumb_data.empty()) {
                std::string thumb_key = hash + ":" + std::to_string(thumb_size);
                if (!store_key_data(thumb_key, thumb_data)) {
                    fprintf(stderr, "Error: Failed to store thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
                    thumbnails_generated = false;
                }
            } else {
                thumbnails_generated = false;
            }
        }
    }
    
    return thumbnails_generated;
}

int DatabaseManager::scan_directory(const std::string& directory, Timer& timer, StatusReporter& reporter) {
    if (!is_open_) return -1;
    
    timer.start("Directory Scanning");
    reporter.update_status("Scanning directory for images...");
    
    // Count total image files first
    std::vector<std::string> image_files;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (entry.is_regular_file() && is_image_file(entry.path().string())) {
                image_files.push_back(entry.path().string());
            }
        }
    } catch (const fs::filesystem_error& ex) {
        return -1;
    }
    
    timer.stop("Directory Scanning");
    
    if (image_files.empty()) {
        reporter.update_status("No image files found");
        return 0;
    }
    
    reporter.set_total_count(image_files.size());
    reporter.update_status("Processing images...");
    
    if (!begin_transaction()) {
        return -1;
    }
    
    int processed_count = 0;
    int skipped_count = 0;
    
    timer.start("Image Processing");
    
    for (size_t i = 0; i < image_files.size(); i++) {
        const std::string& filepath = image_files[i];
        reporter.set_current_count(i + 1);
        
        timer.start("Image Loading");
        
        // Load image with stb_image
        int width, height, channels;
        unsigned char* image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
        if (!image_data) {
            fprintf(stderr, "Error: Failed to load image '%s': %s\n", filepath.c_str(), stbi_failure_reason());
            skipped_count++;
            timer.stop("Image Loading");
            continue;
        }
        
        // Check for zero width or height
        if (width <= 0 || height <= 0) {
            fprintf(stderr, "Error: Invalid image dimensions for '%s': %dx%d\n", filepath.c_str(), width, height);
            stbi_image_free(image_data);
            skipped_count++;
            timer.stop("Image Loading");
            continue;
        }
        
        timer.stop("Image Loading");
        timer.start("Hash Computation");
        
        // Compute content hash
        size_t data_size = width * height * channels;
        XXH64_hash_t hash = XXH64(image_data, data_size, 0);
        char hash_str[17];
        snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);
        
        timer.stop("Hash Computation");
        
        // Check if this hash already exists (duplicate detection)
        std::string path_key = std::string(hash_str) + ":path";
        std::string existing_path;
        if (get_key_value(path_key, existing_path)) {
            stbi_image_free(image_data);
            skipped_count++;
            continue;
        }
        
        timer.start("Database Update");
        
        // Store file path
        if (!store_key_value(path_key, filepath)) {
            stbi_image_free(image_data);
            continue;
        }
        
        timer.stop("Database Update");
        timer.start("Thumbnail Generation");
        
        // Generate and store thumbnails
        bool thumbnails_ok = generate_thumbnails(filepath, hash_str, image_data, width, height, channels);
        
        timer.stop("Thumbnail Generation");
        
        stbi_image_free(image_data);
        
        if (thumbnails_ok) {
            processed_count++;
        } else {
            skipped_count++;
        }
    }
    
    timer.stop("Image Processing");
    timer.start("Database Commit");
    
    bool success = commit_transaction();
    
    timer.stop("Database Commit");
    
    if (!success) {
        return -1;
    }
    
    reporter.update_status("Scanning complete");
    return processed_count;
}

std::string DatabaseManager::extract_hash_from_key(const char* key, size_t key_size) {
    std::string key_str(key, key_size);
    if (key_str.length() > 5 && key_str.substr(key_str.length() - 5) == ":path") {
        return key_str.substr(0, key_str.length() - 5);
    }
    return "";
}

bool DatabaseManager::load_image_info(const std::string& hash, ImageInfo& info) {
    // Find largest thumbnail size available
    std::vector<int> sizes = {32, 64, 128, 256, 512, 1024};
    int best_size = 0;
    std::vector<uint8_t> best_data;

    for (int size : sizes) {
        std::string thumb_key = hash + ":" + std::to_string(size);
        std::vector<uint8_t> data;
        if (get_key_data(thumb_key, data)) {
            best_size = size;
            best_data = std::move(data);
        }
    }

    if (best_size == 0) {
        return false;
    }

    // Load thumbnail image to get dimensions and aspect ratio
    int width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(best_data.data(), best_data.size(),
                                            &width, &height, &channels, 3);

    if (!pixels) {
        fprintf(stderr, "Error: Failed to load thumbnail from memory for hash '%s': %s\n", hash.c_str(), stbi_failure_reason());
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

std::vector<ImageInfo> DatabaseManager::get_all_images() {
    std::vector<ImageInfo> images;
    
    if (!is_open_) return images;
    
    MDB_txn* read_txn;
    MDB_dbi read_dbi;
    MDB_cursor* cursor;
    
    if (mdb_txn_begin(env_, nullptr, MDB_RDONLY, &read_txn) != 0) {
        return images;
    }
    
    if (mdb_dbi_open(read_txn, nullptr, 0, &read_dbi) != 0) {
        mdb_txn_abort(read_txn);
        return images;
    }
    
    if (mdb_cursor_open(read_txn, read_dbi, &cursor) != 0) {
        mdb_txn_abort(read_txn);
        return images;
    }
    
    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
        std::string hash = extract_hash_from_key((char*)key.mv_data, key.mv_size);
        if (!hash.empty()) {
            ImageInfo info;
            info.hash = hash;
            info.path = std::string((char*)data.mv_data, data.mv_size);
            
            // Temporarily store current transaction state
            MDB_txn* temp_txn = txn_;
            MDB_dbi temp_dbi = dbi_;
            
            // Use read transaction for loading image info
            txn_ = read_txn;
            dbi_ = read_dbi;
            
            if (load_image_info(hash, info)) {
                images.push_back(std::move(info));
            } else {
                fprintf(stderr, "Warning: Failed to load image info for '%s' (hash: %s)\n", info.path.c_str(), hash.c_str());
            }
            
            // Restore transaction state
            txn_ = temp_txn;
            dbi_ = temp_dbi;
        }
    }
    
    mdb_cursor_close(cursor);
    mdb_txn_abort(read_txn);
    
    // Sort images alphabetically by path
    std::sort(images.begin(), images.end(),
             [](const ImageInfo& a, const ImageInfo& b) {
                 return a.path < b.path;
             });
    
    return images;
}