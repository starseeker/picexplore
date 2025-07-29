/*
 * image_similarity_index.h - Image similarity search using feature vectors
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
#include <unordered_map>
#include "../src/database.hpp"

// Forward declarations for mlpack (using simple approach to avoid complex templates)
class mlpack_search_index;

/**
 * Structure representing a similar image match
 */
struct SimilarImage {
    std::string file_path;
    std::string hash;
    double similarity_score;
    
    SimilarImage(const std::string& path, const std::string& h, double score)
        : file_path(path), hash(h), similarity_score(score) {}
};

/**
 * Configuration options for the ImageSimilarityIndex
 */
struct SimilarityConfig {
    std::string model_path;           // Path to ONNX model file
    std::string cache_db_path;        // Optional LMDB cache path for feature vectors
    bool use_cache = true;            // Whether to use LMDB caching
    bool use_lsh = true;              // Use LSH instead of KNN for large datasets
    int feature_vector_size = 1280;   // Expected feature vector size (MobileNetV2/V3)
    
    SimilarityConfig() = default;
    SimilarityConfig(const std::string& model) : model_path(model) {}
};

/**
 * ImageSimilarityIndex - Main class for image similarity search
 * 
 * This class provides content-based image similarity search using:
 * - ONNX Runtime for feature extraction with MobileNetV2/V3 models
 * - mlpack for efficient similarity search (LSH or KNN)
 * - Optional LMDB caching for computed feature vectors
 * - Integration with the picscan database format
 */
class ImageSimilarityIndex {
public:
    ImageSimilarityIndex(const SimilarityConfig& config);
    ~ImageSimilarityIndex();
    
    /**
     * Load images from a picscan database and build the similarity index
     * @param picscan_db_path Path to the picscan LMDB database
     * @return true on success, false on failure
     */
    bool load_database(const std::string& picscan_db_path);
    
    /**
     * Find similar images to a query image
     * @param query_image_path Path to query JPEG image
     * @param similarity_threshold Minimum similarity score (0.0 to 1.0)
     * @param max_results Maximum number of results to return (0 = unlimited)
     * @return Vector of similar images sorted by similarity (most similar first)
     */
    std::vector<SimilarImage> find_similar(const std::string& query_image_path, 
                                          double similarity_threshold = 0.7,
                                          int max_results = 100);
    
    /**
     * Get the number of images in the index
     */
    size_t size() const { return image_hashes_.size(); }
    
    /**
     * Check if the index is ready for queries
     */
    bool is_ready() const { return is_initialized_; }
    
private:
    SimilarityConfig config_;
    bool is_initialized_;
    
    // Image data and feature vectors
    std::vector<std::string> image_hashes_;
    std::unordered_map<std::string, std::string> hash_to_path_;
    std::vector<std::vector<float>> feature_vectors_;
    
    // mlpack search structures
    void* search_index_;  // Raw pointer for now, properly managed in implementation
    
    // Feature vector cache (optional LMDB)
    std::unique_ptr<class FeatureCache> feature_cache_;
    
    /**
     * Initialize the similarity index (placeholder for future ONNX Runtime integration)
     */
    bool initialize_features();
    
    /**
     * Extract feature vector from image data using ONNX model
     * @param image_data RGB image data
     * @param width Image width
     * @param height Image height  
     * @param feature_vector Output feature vector
     * @return true on success
     */
    bool extract_features(const uint8_t* image_data, int width, int height,
                         std::vector<float>& feature_vector);
    
    /**
     * Process a thumbnail from the database and extract features
     * @param hash Image hash
     * @param thumb_data JPEG thumbnail data
     * @return true on success
     */
    bool process_thumbnail(const std::string& hash, const std::vector<uint8_t>& thumb_data);
    
    /**
     * Build the search index from loaded feature vectors
     */
    bool build_search_index();
    
    /**
     * Preprocess image for model input (resize, normalize, etc.)
     * @param image_data Input RGB data
     * @param width Input width
     * @param height Input height
     * @param output_data Preprocessed data ready for model
     * @param output_width Model input width
     * @param output_height Model input height
     * @return true on success
     */
    bool preprocess_image(const uint8_t* image_data, int width, int height,
                         std::vector<float>& output_data, int& output_width, int& output_height);
    
    /**
     * Calculate cosine similarity between two feature vectors
     * @param vec1 First feature vector
     * @param vec2 Second feature vector
     * @return Similarity score (0.0 to 1.0)
     */
    static double calculate_cosine_similarity(const std::vector<float>& vec1, 
                                            const std::vector<float>& vec2);
};

/**
 * Feature vector cache using LMDB for persistent storage
 * This is an internal helper class for caching computed feature vectors
 */
class FeatureCache {
public:
    FeatureCache(const std::string& cache_path);
    ~FeatureCache();
    
    bool open();
    void close();
    
    bool get_features(const std::string& hash, std::vector<float>& features);
    bool store_features(const std::string& hash, const std::vector<float>& features);
    
private:
    std::string cache_path_;
    void* env_;  // MDB_env* 
    void* dbi_;  // MDB_dbi
    bool is_open_;
};