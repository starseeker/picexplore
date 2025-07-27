/*
 * image_thumb_db.cpp - Standalone image scanner/thumbnailer/database processor
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
#include <filesystem>
#include <algorithm>
#include <cstring>

// Third-party dependencies
#include "cxxopts.hpp"      // Command line parsing
#include "xxhash.h"         // Fast hashing for image content identification
#include "lmdb.h"          // Lightning Memory-Mapped Database
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"     // Image loading
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h" // Image resizing
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h" // Image writing
#include "Epeg.h"          // JPEG thumbnailing

namespace fs = std::filesystem;

// Helper function to encode RGB data as JPEG
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality = 90) {
    std::vector<uint8_t> jpeg_data;
    
    // STB write callback to capture data
    auto write_func = [](void* context, void* data, int size) {
        auto* vec = static_cast<std::vector<uint8_t>*>(context);
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        vec->insert(vec->end(), bytes, bytes + size);
    };
    
    if (stbi_write_jpg_to_func(write_func, &jpeg_data, width, height, 3, rgb_data, quality)) {
        return jpeg_data;
    }
    
    return {}; // Empty vector on failure
}

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("image_thumb_db", "Standalone image scanner/thumbnailer/database processor");
        
        options.add_options()
            ("h,help", "Print usage")
            ("d,directory", "Directory to scan for images", cxxopts::value<std::string>())
            ("o,output", "Output database path", cxxopts::value<std::string>()->default_value("./images.db"))
            ("v,verbose", "Enable verbose output")
        ;

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            return 0;
        }

        std::string directory = ".";
        if (result.count("directory")) {
            directory = result["directory"].as<std::string>();
        }

        std::string db_path = result["output"].as<std::string>();
        bool verbose = result.count("verbose") > 0;

        if (verbose) {
            std::cout << "Scanning directory: " << directory << std::endl;
            std::cout << "Output database: " << db_path << std::endl;
        }

        // Basic functionality demonstration
        std::cout << "Image Thumb DB Scanner v1.0" << std::endl;
        
        // Test xxHash functionality
        const char* test_data = "Hello, World!";
        XXH64_hash_t hash = XXH64(test_data, strlen(test_data), 0);
        if (verbose) {
            std::cout << "Test hash (xxHash): " << std::hex << hash << std::dec << std::endl;
        }

        // Initialize LMDB database
        MDB_env *env;
        MDB_txn *txn;
        MDB_dbi dbi;
        
        int rc = mdb_env_create(&env);
        if (rc != 0) {
            std::cerr << "Error: Failed to create LMDB environment: " << mdb_strerror(rc) << std::endl;
            return 1;
        }
        
        // Set map size to handle large databases (1GB)
        rc = mdb_env_set_mapsize(env, 1024 * 1024 * 1024);
        if (rc != 0) {
            std::cerr << "Error: Failed to set LMDB map size: " << mdb_strerror(rc) << std::endl;
            mdb_env_close(env);
            return 1;
        }
        
        rc = mdb_env_open(env, db_path.c_str(), MDB_NOSUBDIR, 0664);
        if (rc != 0) {
            std::cerr << "Error: Failed to open LMDB database: " << mdb_strerror(rc) << std::endl;
            mdb_env_close(env);
            return 1;
        }
        
        rc = mdb_txn_begin(env, nullptr, 0, &txn);
        if (rc != 0) {
            std::cerr << "Error: Failed to begin transaction: " << mdb_strerror(rc) << std::endl;
            mdb_env_close(env);
            return 1;
        }
        
        rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, &dbi);
        if (rc != 0) {
            std::cerr << "Error: Failed to open database: " << mdb_strerror(rc) << std::endl;
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return 1;
        }

        if (verbose) {
            std::cout << "Successfully opened LMDB database at: " << db_path << std::endl;
        }

        // Thumbnail sizes to generate (must match what thumb_gallery_pdf expects)
        std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024};
        
        // Scan for image files and process them
        int image_count = 0;
        int processed_count = 0;
        int skipped_count = 0;
        
        try {
            for (const auto& entry : fs::recursive_directory_iterator(directory)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" || ext == ".tga") {
                        image_count++;
                        std::string filepath = entry.path().string();
                        
                        if (verbose) {
                            std::cout << "Processing image: " << filepath << std::endl;
                        }
                        
                        // Load image with stb_image
                        int width, height, channels;
                        unsigned char* image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
                        if (!image_data) {
                            if (verbose) {
                                std::cout << "  Warning: Failed to load image, skipping" << std::endl;
                            }
                            skipped_count++;
                            continue;
                        }
                        
                        // Compute content hash 
                        size_t data_size = width * height * channels;
                        XXH64_hash_t hash = XXH64(image_data, data_size, 0);
                        char hash_str[17];
                        snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);
                        
                        if (verbose) {
                            std::cout << "  Dimensions: " << width << "x" << height << " (" << channels << " channels)" << std::endl;
                            std::cout << "  Hash: " << hash_str << std::endl;
                        }
                        
                        // Check if this hash already exists (duplicate detection)
                        std::string path_key = std::string(hash_str) + ":path";
                        MDB_val key, data;
                        key.mv_data = (void*)path_key.c_str();
                        key.mv_size = path_key.length();
                        
                        if (mdb_get(txn, dbi, &key, &data) == 0) {
                            if (verbose) {
                                std::cout << "  Duplicate image (same hash), skipping" << std::endl;
                            }
                            stbi_image_free(image_data);
                            skipped_count++;
                            continue;
                        }
                        
                        // Store file path
                        data.mv_data = (void*)filepath.c_str();
                        data.mv_size = filepath.length();
                        rc = mdb_put(txn, dbi, &key, &data, 0);
                        if (rc != 0) {
                            std::cerr << "Error: Failed to store path for " << filepath << ": " << mdb_strerror(rc) << std::endl;
                            stbi_image_free(image_data);
                            continue;
                        }
                        
                        // Generate and store thumbnails
                        bool thumbnails_generated = true;
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
                                if (verbose) {
                                    std::cout << "  Skipping " << thumb_size << "px thumbnail (original is smaller)" << std::endl;
                                }
                                continue;
                            }
                            
                            // Use epeg for JPEG thumbnails if source is JPEG
                            std::vector<uint8_t> thumb_data;
                            bool thumb_success = false;
                            
                            if (ext == ".jpg" || ext == ".jpeg") {
                                // Use epeg for efficient JPEG thumbnailing
                                Epeg_Image *im = epeg_file_open(filepath.c_str());
                                if (im) {
                                    epeg_decode_size_set(im, thumb_width, thumb_height);
                                    epeg_quality_set(im, 90);
                                    
                                    // Write to memory buffer
                                    unsigned char *thumb_buffer;
                                    int thumb_buffer_size;
                                    epeg_memory_output_set(im, &thumb_buffer, &thumb_buffer_size);
                                    if (epeg_encode(im) == 0) {
                                        thumb_data.assign(thumb_buffer, thumb_buffer + thumb_buffer_size);
                                        thumb_success = true;
                                        free(thumb_buffer);
                                    }
                                    epeg_close(im);
                                }
                            }
                            
                            // Fallback: use stb_image_resize for non-JPEG or if epeg failed
                            if (!thumb_success) {
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
                                    }
                                }
                                
                                if (need_free_rgb) {
                                    free(rgb_data);
                                }
                            }
                            
                            if (thumb_success && !thumb_data.empty()) {
                                // Store thumbnail in LMDB
                                std::string thumb_key = std::string(hash_str) + ":" + std::to_string(thumb_size);
                                MDB_val t_key, t_data;
                                t_key.mv_data = (void*)thumb_key.c_str();
                                t_key.mv_size = thumb_key.length();
                                t_data.mv_data = thumb_data.data();
                                t_data.mv_size = thumb_data.size();
                                
                                rc = mdb_put(txn, dbi, &t_key, &t_data, 0);
                                if (rc != 0) {
                                    std::cerr << "Error: Failed to store " << thumb_size << "px thumbnail: " << mdb_strerror(rc) << std::endl;
                                    thumbnails_generated = false;
                                } else if (verbose) {
                                    std::cout << "  Generated " << thumb_size << "px thumbnail (" << thumb_data.size() << " bytes)" << std::endl;
                                }
                            }
                        }
                        
                        stbi_image_free(image_data);
                        
                        if (thumbnails_generated) {
                            processed_count++;
                        } else {
                            skipped_count++;
                        }
                    }
                }
            }
        } catch (const fs::filesystem_error& ex) {
            std::cerr << "Filesystem error: " << ex.what() << std::endl;
            mdb_txn_abort(txn);
            mdb_env_close(env);
            return 1;
        }

        // Commit transaction
        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            std::cerr << "Error: Failed to commit transaction: " << mdb_strerror(rc) << std::endl;
            mdb_env_close(env);
            return 1;
        }
        
        mdb_env_close(env);
        
        std::cout << "Processing complete:" << std::endl;
        std::cout << "  Found: " << image_count << " image files" << std::endl;
        std::cout << "  Processed: " << processed_count << " images" << std::endl;
        std::cout << "  Skipped: " << skipped_count << " images (duplicates or errors)" << std::endl;

        return 0;
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}