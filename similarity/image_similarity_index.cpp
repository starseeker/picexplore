/*
 * image_similarity_index.cpp - Image similarity search using feature vectors
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

#include "image_similarity_index.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <filesystem>

// Third-party dependencies
#include "lmdb.h"
#include "stb_image.h"
#include "xxhash.h"

// mlpack includes
#include <mlpack.hpp>

namespace fs = std::filesystem;

// TODO: Replace with ONNX Runtime implementation
// For now, we'll use a simple feature extraction based on color histograms
// and basic texture features as a placeholder for MobileNetV2/V3 features

ImageSimilarityIndex::ImageSimilarityIndex(const SimilarityConfig& config)
    : config_(config), is_initialized_(false), search_index_(nullptr) {
    
    // Initialize feature cache if enabled
    if (config_.use_cache && !config_.cache_db_path.empty()) {
        feature_cache_ = std::make_unique<FeatureCache>(config_.cache_db_path);
        if (!feature_cache_->open()) {
            std::cerr << "Warning: Failed to open feature cache, proceeding without caching" << std::endl;
            feature_cache_.reset();
        }
    }
}

ImageSimilarityIndex::~ImageSimilarityIndex() {
    if (search_index_) {
        delete static_cast<mlpack::NeighborSearch<>*>(search_index_);
        search_index_ = nullptr;
    }
    
    if (feature_cache_) {
        feature_cache_->close();
    }
}

bool ImageSimilarityIndex::load_database(const std::string& picscan_db_path) {
    if (!fs::exists(picscan_db_path)) {
        std::cerr << "Error: picscan database does not exist: " << picscan_db_path << std::endl;
        return false;
    }
    
    std::cout << "Loading images from picscan database: " << picscan_db_path << std::endl;
    
    // Open the picscan LMDB database
    MDB_env* env = nullptr;
    MDB_txn* txn = nullptr;
    MDB_dbi dbi = 0;
    MDB_cursor* cursor = nullptr;
    
    int rc = mdb_env_create(&env);
    if (rc != 0) {
        std::cerr << "Error: Failed to create LMDB environment: " << mdb_strerror(rc) << std::endl;
        return false;
    }
    
    rc = mdb_env_set_mapsize(env, 549755813888); // Same as original database.cpp
    if (rc != 0) {
        std::cerr << "Error: Failed to set LMDB map size: " << mdb_strerror(rc) << std::endl;
        mdb_env_close(env);
        return false;
    }
    
    rc = mdb_env_open(env, picscan_db_path.c_str(), MDB_RDONLY | MDB_NOSUBDIR, 0664);
    if (rc != 0) {
        std::cerr << "Error: Failed to open LMDB database: " << mdb_strerror(rc) << std::endl;
        mdb_env_close(env);
        return false;
    }
    
    rc = mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        std::cerr << "Error: Failed to begin LMDB transaction: " << mdb_strerror(rc) << std::endl;
        mdb_env_close(env);
        return false;
    }
    
    rc = mdb_dbi_open(txn, nullptr, 0, &dbi);
    if (rc != 0) {
        std::cerr << "Error: Failed to open LMDB DBI: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return false;
    }
    
    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        std::cerr << "Error: Failed to open LMDB cursor: " << mdb_strerror(rc) << std::endl;
        mdb_txn_abort(txn);
        mdb_env_close(env);
        return false;
    }
    
    // Iterate through all entries to find thumbnails and paths
    MDB_val key, data;
    std::unordered_map<std::string, std::vector<uint8_t>> largest_thumbs;
    
    int processed_count = 0;
    while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
        std::string key_str((char*)key.mv_data, key.mv_size);
        
        // Check if this is a thumbnail entry (format: "hash:size")
        size_t colon_pos = key_str.find(':');
        if (colon_pos != std::string::npos) {
            std::string hash = key_str.substr(0, colon_pos);
            std::string suffix = key_str.substr(colon_pos + 1);
            
            if (suffix == "path") {
                // This is a path entry (format: "hash:path")
                std::string path((char*)data.mv_data, data.mv_size);
                hash_to_path_[hash] = path;
            } else {
                // This might be a thumbnail entry - try to parse the size
                try {
                    int size = std::stoi(suffix);
                    // We want the largest available thumbnail (1024px preferred, but take what we get)
                    if (size >= 256) { // Only consider reasonably sized thumbnails
                        if (largest_thumbs.find(hash) == largest_thumbs.end() || size > 256) {
                            std::vector<uint8_t> thumb_data((uint8_t*)data.mv_data, 
                                                           (uint8_t*)data.mv_data + data.mv_size);
                            largest_thumbs[hash] = std::move(thumb_data);
                        }
                    }
                } catch (const std::exception&) {
                    // Not a size, ignore
                }
            }
        }
        
        processed_count++;
    }
    
    mdb_cursor_close(cursor);
    mdb_txn_abort(txn);
    mdb_env_close(env);
    
    std::cout << "Found " << largest_thumbs.size() << " images with thumbnails" << std::endl;
    
    // Process thumbnails and extract features
    int feature_count = 0;
    for (const auto& [hash, thumb_data] : largest_thumbs) {
        if (hash_to_path_.find(hash) != hash_to_path_.end()) {
            if (process_thumbnail(hash, thumb_data)) {
                feature_count++;
            }
        }
    }
    
    std::cout << "Successfully extracted features from " << feature_count << " images" << std::endl;
    
    if (feature_count == 0) {
        std::cerr << "Error: No features extracted from database" << std::endl;
        return false;
    }
    
    // Build the search index
    if (!build_search_index()) {
        std::cerr << "Error: Failed to build search index" << std::endl;
        return false;
    }
    
    is_initialized_ = true;
    std::cout << "Image similarity index ready with " << size() << " images" << std::endl;
    return true;
}

bool ImageSimilarityIndex::process_thumbnail(const std::string& hash, const std::vector<uint8_t>& thumb_data) {
    // Check cache first
    std::vector<float> cached_features;
    if (feature_cache_ && feature_cache_->get_features(hash, cached_features)) {
        image_hashes_.push_back(hash);
        feature_vectors_.push_back(std::move(cached_features));
        return true;
    }
    
    // Decode the JPEG thumbnail
    int width, height, channels;
    uint8_t* image_data = stbi_load_from_memory(thumb_data.data(), thumb_data.size(), 
                                               &width, &height, &channels, 3); // Force RGB
    if (!image_data) {
        std::cerr << "Warning: Failed to decode thumbnail for hash: " << hash << std::endl;
        return false;
    }
    
    // Extract features from the image
    std::vector<float> feature_vector;
    bool success = extract_features(image_data, width, height, feature_vector);
    
    stbi_image_free(image_data);
    
    if (!success) {
        std::cerr << "Warning: Failed to extract features for hash: " << hash << std::endl;
        return false;
    }
    
    // Store in cache if available
    if (feature_cache_) {
        feature_cache_->store_features(hash, feature_vector);
    }
    
    // Add to our index
    image_hashes_.push_back(hash);
    feature_vectors_.push_back(std::move(feature_vector));
    
    return true;
}

bool ImageSimilarityIndex::extract_features(const uint8_t* image_data, int width, int height,
                                          std::vector<float>& feature_vector) {
    // TODO: Replace this with ONNX Runtime + MobileNetV2/V3 implementation
    // For now, we'll compute a simple feature vector based on:
    // 1. Color histogram (RGB, 16 bins each = 48 features)
    // 2. Basic texture features (8 features)
    // 3. Simple spatial features (8 features)
    // Total: 64 features (much smaller than MobileNet's 1280, but sufficient for demo)
    
    feature_vector.clear();
    feature_vector.reserve(64);
    
    // 1. Color histogram features (16 bins each for R, G, B = 48 features)
    std::vector<int> r_hist(16, 0), g_hist(16, 0), b_hist(16, 0);
    
    for (int i = 0; i < width * height; i++) {
        int r = image_data[i * 3 + 0];
        int g = image_data[i * 3 + 1];
        int b = image_data[i * 3 + 2];
        
        r_hist[r / 16]++;
        g_hist[g / 16]++;
        b_hist[b / 16]++;
    }
    
    // Normalize histograms
    int total_pixels = width * height;
    for (int i = 0; i < 16; i++) {
        feature_vector.push_back((float)r_hist[i] / total_pixels);
        feature_vector.push_back((float)g_hist[i] / total_pixels);
        feature_vector.push_back((float)b_hist[i] / total_pixels);
    }
    
    // 2. Basic texture features (8 features)
    float mean_intensity = 0;
    for (int i = 0; i < width * height; i++) {
        float gray = (image_data[i*3] + image_data[i*3+1] + image_data[i*3+2]) / 3.0f;
        mean_intensity += gray;
    }
    mean_intensity /= total_pixels;
    
    float variance = 0;
    for (int i = 0; i < width * height; i++) {
        float gray = (image_data[i*3] + image_data[i*3+1] + image_data[i*3+2]) / 3.0f;
        variance += (gray - mean_intensity) * (gray - mean_intensity);
    }
    variance /= total_pixels;
    
    feature_vector.push_back(mean_intensity / 255.0f);    // Normalized mean
    feature_vector.push_back(std::sqrt(variance) / 255.0f); // Normalized std dev
    
    // Simple edge detection (horizontal and vertical gradients)
    float h_edges = 0, v_edges = 0;
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float gray = (image_data[idx*3] + image_data[idx*3+1] + image_data[idx*3+2]) / 3.0f;
            float gray_left = (image_data[(idx-1)*3] + image_data[(idx-1)*3+1] + image_data[(idx-1)*3+2]) / 3.0f;
            float gray_right = (image_data[(idx+1)*3] + image_data[(idx+1)*3+1] + image_data[(idx+1)*3+2]) / 3.0f;
            float gray_up = (image_data[(idx-width)*3] + image_data[(idx-width)*3+1] + image_data[(idx-width)*3+2]) / 3.0f;
            float gray_down = (image_data[(idx+width)*3] + image_data[(idx+width)*3+1] + image_data[(idx+width)*3+2]) / 3.0f;
            
            h_edges += std::abs(gray_right - gray_left);
            v_edges += std::abs(gray_down - gray_up);
        }
    }
    
    feature_vector.push_back(h_edges / (total_pixels * 255.0f));
    feature_vector.push_back(v_edges / (total_pixels * 255.0f));
    
    // Corner-like features (simplified)
    float corners = 0;
    for (int y = 1; y < height - 1; y++) {
        for (int x = 1; x < width - 1; x++) {
            int idx = y * width + x;
            float center = (image_data[idx*3] + image_data[idx*3+1] + image_data[idx*3+2]) / 3.0f;
            
            // Check 4 neighbors
            float n1 = (image_data[(idx-1)*3] + image_data[(idx-1)*3+1] + image_data[(idx-1)*3+2]) / 3.0f;
            float n2 = (image_data[(idx+1)*3] + image_data[(idx+1)*3+1] + image_data[(idx+1)*3+2]) / 3.0f;
            float n3 = (image_data[(idx-width)*3] + image_data[(idx-width)*3+1] + image_data[(idx-width)*3+2]) / 3.0f;
            float n4 = (image_data[(idx+width)*3] + image_data[(idx+width)*3+1] + image_data[(idx+width)*3+2]) / 3.0f;
            
            float corner_response = std::abs(center - n1) + std::abs(center - n2) + 
                                  std::abs(center - n3) + std::abs(center - n4);
            corners += corner_response;
        }
    }
    
    feature_vector.push_back(corners / (total_pixels * 255.0f * 4.0f));
    feature_vector.push_back((float)width / std::max(width, height)); // Aspect ratio component
    feature_vector.push_back((float)height / std::max(width, height));
    feature_vector.push_back(0.0f); // Reserved for future features
    
    return true;
}

bool ImageSimilarityIndex::build_search_index() {
    if (feature_vectors_.empty()) {
        return false;
    }
    
    std::cout << "Building similarity search index..." << std::endl;
    
    // Convert feature vectors to mlpack matrix format
    size_t num_features = feature_vectors_[0].size();
    size_t num_images = feature_vectors_.size();
    
    arma::mat dataset(num_features, num_images);
    for (size_t i = 0; i < num_images; i++) {
        for (size_t j = 0; j < num_features; j++) {
            dataset(j, i) = feature_vectors_[i][j];
        }
    }
    
    // For now, use simple KNN (can be switched to LSH for larger datasets)
    // TODO: Add LSH implementation for datasets > 10000 images
    auto* knn_search = new mlpack::NeighborSearch<>(std::move(dataset));
    search_index_ = knn_search;
    
    std::cout << "Search index built successfully" << std::endl;
    return true;
}

std::vector<SimilarImage> ImageSimilarityIndex::find_similar(const std::string& query_image_path,
                                                            double similarity_threshold,
                                                            int max_results) {
    std::vector<SimilarImage> results;
    
    if (!is_initialized_) {
        std::cerr << "Error: Index not initialized" << std::endl;
        return results;
    }
    
    if (!fs::exists(query_image_path)) {
        std::cerr << "Error: Query image does not exist: " << query_image_path << std::endl;
        return results;
    }
    
    // Load and process query image
    int width, height, channels;
    uint8_t* image_data = stbi_load(query_image_path.c_str(), &width, &height, &channels, 3);
    if (!image_data) {
        std::cerr << "Error: Failed to load query image: " << query_image_path << std::endl;
        return results;
    }
    
    // Extract features from query image
    std::vector<float> query_features;
    bool success = extract_features(image_data, width, height, query_features);
    stbi_image_free(image_data);
    
    if (!success) {
        std::cerr << "Error: Failed to extract features from query image" << std::endl;
        return results;
    }
    
    // Search for similar images
    arma::mat query_mat(query_features.size(), 1);
    for (size_t i = 0; i < query_features.size(); i++) {
        query_mat(i, 0) = query_features[i];
    }
    
    arma::Mat<size_t> neighbors;
    arma::mat distances;
    
    // Get more neighbors than needed, then filter by threshold
    int k = (max_results > 0) ? std::min(max_results * 2, (int)size()) : size();
    
    auto* knn_search = static_cast<mlpack::NeighborSearch<>*>(search_index_);
    knn_search->Search(query_mat, k, neighbors, distances);
    
    // Convert distances to similarity scores and filter
    for (size_t i = 0; i < neighbors.n_elem && (max_results == 0 || results.size() < max_results); i++) {
        size_t neighbor_idx = neighbors(i);
        double distance = distances(i);
        
        // Convert Euclidean distance to cosine similarity approximation
        // For normalized features, this works reasonably well
        double similarity = std::max(0.0, 1.0 - distance / 10.0); // Normalize distance
        
        if (similarity >= similarity_threshold) {
            std::string hash = image_hashes_[neighbor_idx];
            std::string path = hash_to_path_[hash];
            results.emplace_back(path, hash, similarity);
        }
    }
    
    // Sort by similarity (highest first)
    std::sort(results.begin(), results.end(), 
              [](const SimilarImage& a, const SimilarImage& b) {
                  return a.similarity_score > b.similarity_score;
              });
    
    return results;
}

double ImageSimilarityIndex::calculate_cosine_similarity(const std::vector<float>& vec1, 
                                                        const std::vector<float>& vec2) {
    if (vec1.size() != vec2.size()) {
        return 0.0;
    }
    
    double dot_product = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    
    for (size_t i = 0; i < vec1.size(); i++) {
        dot_product += vec1[i] * vec2[i];
        norm_a += vec1[i] * vec1[i];
        norm_b += vec2[i] * vec2[i];
    }
    
    if (norm_a == 0.0 || norm_b == 0.0) {
        return 0.0;
    }
    
    return dot_product / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// FeatureCache implementation
FeatureCache::FeatureCache(const std::string& cache_path) 
    : cache_path_(cache_path), env_(nullptr), dbi_(nullptr), is_open_(false) {
}

FeatureCache::~FeatureCache() {
    close();
}

bool FeatureCache::open() {
    MDB_env** env_ptr = (MDB_env**)&env_;
    
    int rc = mdb_env_create(env_ptr);
    if (rc != 0) {
        std::cerr << "Error: Failed to create cache LMDB environment: " << mdb_strerror(rc) << std::endl;
        return false;
    }
    
    rc = mdb_env_set_mapsize(*env_ptr, 1073741824); // 1GB for cache
    if (rc != 0) {
        std::cerr << "Error: Failed to set cache LMDB map size: " << mdb_strerror(rc) << std::endl;
        mdb_env_close(*env_ptr);
        env_ = nullptr;
        return false;
    }
    
    rc = mdb_env_open(*env_ptr, cache_path_.c_str(), MDB_NOSUBDIR, 0664);
    if (rc != 0) {
        std::cerr << "Error: Failed to open cache LMDB database: " << mdb_strerror(rc) << std::endl;
        mdb_env_close(*env_ptr);
        env_ = nullptr;
        return false;
    }
    
    // Open DBI
    MDB_txn* txn;
    rc = mdb_txn_begin(*env_ptr, nullptr, 0, &txn);
    if (rc != 0) {
        mdb_env_close(*env_ptr);
        env_ = nullptr;
        return false;
    }
    
    MDB_dbi* dbi_ptr = (MDB_dbi*)&dbi_;
    rc = mdb_dbi_open(txn, nullptr, MDB_CREATE, dbi_ptr);
    if (rc != 0) {
        mdb_txn_abort(txn);
        mdb_env_close(*env_ptr);
        env_ = nullptr;
        return false;
    }
    
    rc = mdb_txn_commit(txn);
    if (rc != 0) {
        mdb_env_close(*env_ptr);
        env_ = nullptr;
        return false;
    }
    
    is_open_ = true;
    return true;
}

void FeatureCache::close() {
    if (env_) {
        mdb_env_close((MDB_env*)env_);
        env_ = nullptr;
    }
    is_open_ = false;
}

bool FeatureCache::get_features(const std::string& hash, std::vector<float>& features) {
    if (!is_open_) return false;
    
    MDB_txn* txn;
    int rc = mdb_txn_begin((MDB_env*)env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) return false;
    
    MDB_val key, data;
    key.mv_size = hash.size();
    key.mv_data = (void*)hash.c_str();
    
    rc = mdb_get(txn, *(MDB_dbi*)&dbi_, &key, &data);
    if (rc == 0) {
        // Data found, convert back to vector
        size_t num_features = data.mv_size / sizeof(float);
        features.resize(num_features);
        memcpy(features.data(), data.mv_data, data.mv_size);
    }
    
    mdb_txn_abort(txn);
    return (rc == 0);
}

bool FeatureCache::store_features(const std::string& hash, const std::vector<float>& features) {
    if (!is_open_) return false;
    
    MDB_txn* txn;
    int rc = mdb_txn_begin((MDB_env*)env_, nullptr, 0, &txn);
    if (rc != 0) return false;
    
    MDB_val key, data;
    key.mv_size = hash.size();
    key.mv_data = (void*)hash.c_str();
    data.mv_size = features.size() * sizeof(float);
    data.mv_data = (void*)features.data();
    
    rc = mdb_put(txn, *(MDB_dbi*)&dbi_, &key, &data, 0);
    if (rc == 0) {
        rc = mdb_txn_commit(txn);
    } else {
        mdb_txn_abort(txn);
    }
    
    return (rc == 0);
}