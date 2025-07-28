/*
 * simfind.cpp - Command line tool for image similarity search
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
#include <chrono>
#include <iomanip>

#include "../third_party/cxxopts/include/cxxopts.hpp"
#include "image_similarity_index.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("simfind", "Find similar images using content-based similarity search");
        
        options.add_options()
            ("h,help", "Print usage")
            ("q,query", "Path to query image (JPEG file)", cxxopts::value<std::string>())
            ("d,database", "Path to picscan LMDB database", cxxopts::value<std::string>())
            ("t,threshold", "Similarity score threshold (0.0-1.0)", cxxopts::value<double>()->default_value("0.7"))
            ("n,max-results", "Maximum number of results (0 = unlimited)", cxxopts::value<int>()->default_value("100"))
            ("m,model", "Path to ONNX model file (optional)", cxxopts::value<std::string>()->default_value(""))
            ("c,cache", "Path to feature cache database (optional)", cxxopts::value<std::string>()->default_value(""))
            ("no-cache", "Disable feature caching")
            ("v,verbose", "Enable verbose output")
        ;

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::cout << "\nDescription:\n";
            std::cout << "  Find images similar to a query image using content-based similarity search.\n";
            std::cout << "  Uses the largest available thumbnails from a picscan database for fast processing.\n\n";
            std::cout << "Examples:\n";
            std::cout << "  # Find similar images with default threshold (0.7)\n";
            std::cout << "  simfind --query photo.jpg --database images.db\n\n";
            std::cout << "  # Find highly similar images with higher threshold\n";
            std::cout << "  simfind --query photo.jpg --database images.db --threshold 0.9\n\n";
            std::cout << "  # Get unlimited results with lower threshold\n";
            std::cout << "  simfind --query photo.jpg --database images.db --threshold 0.5 --max-results 0\n\n";
            std::cout << "  # Use custom feature cache location\n";
            std::cout << "  simfind --query photo.jpg --database images.db --cache /tmp/features.cache\n\n";
            std::cout << "Output:\n";
            std::cout << "  Prints file paths of similar images, one per line, sorted by similarity (most similar first).\n";
            return 0;
        }

        // Validate required arguments
        if (!result.count("query")) {
            std::cerr << "Error: --query is required" << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }
        
        if (!result.count("database")) {
            std::cerr << "Error: --database is required" << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            return 1;
        }

        std::string query_path = result["query"].as<std::string>();
        std::string db_path = result["database"].as<std::string>();
        double threshold = result["threshold"].as<double>();
        int max_results = result["max-results"].as<int>();
        std::string model_path = result["model"].as<std::string>();
        std::string cache_path = result["cache"].as<std::string>();
        bool use_cache = !result.count("no-cache");
        bool verbose = result.count("verbose") > 0;

        // Validate arguments
        if (!fs::exists(query_path)) {
            std::cerr << "Error: Query image does not exist: " << query_path << std::endl;
            return 1;
        }
        
        if (!fs::exists(db_path)) {
            std::cerr << "Error: Database does not exist: " << db_path << std::endl;
            return 1;
        }
        
        if (threshold < 0.0 || threshold > 1.0) {
            std::cerr << "Error: Threshold must be between 0.0 and 1.0" << std::endl;
            return 1;
        }
        
        if (max_results < 0) {
            std::cerr << "Error: max-results must be non-negative (0 = unlimited)" << std::endl;
            return 1;
        }
        
        if (!model_path.empty() && !fs::exists(model_path)) {
            std::cerr << "Error: Model file does not exist: " << model_path << std::endl;
            return 1;
        }

        if (verbose) {
            std::cout << "SimFind - Image Similarity Search Tool" << std::endl;
            std::cout << "Query image: " << query_path << std::endl;
            std::cout << "Database: " << db_path << std::endl;
            std::cout << "Similarity threshold: " << threshold << std::endl;
            std::cout << "Max results: " << (max_results == 0 ? "unlimited" : std::to_string(max_results)) << std::endl;
            if (!model_path.empty()) {
                std::cout << "ONNX model: " << model_path << std::endl;
            }
            if (use_cache && !cache_path.empty()) {
                std::cout << "Feature cache: " << cache_path << std::endl;
            }
            std::cout << std::endl;
        }

        // Configure similarity search
        SimilarityConfig config;
        config.model_path = model_path;
        config.use_cache = use_cache;
        
        // Set cache path - use default if not specified
        if (cache_path.empty() && use_cache) {
            // Default cache path next to the database
            fs::path db_dir = fs::path(db_path).parent_path();
            config.cache_db_path = (db_dir / "similarity_features.cache").string();
        } else {
            config.cache_db_path = cache_path;
        }
        
        // TODO: When ONNX Runtime is integrated, detect model type and set appropriate feature vector size
        // For now, using simple features (64 dimensions)
        config.feature_vector_size = 64;

        auto start_time = std::chrono::high_resolution_clock::now();

        // Initialize similarity index
        if (verbose) {
            std::cout << "Initializing image similarity index..." << std::endl;
        }
        
        ImageSimilarityIndex similarity_index(config);
        
        // Load database and build index
        if (verbose) {
            std::cout << "Loading database and extracting features..." << std::endl;
        }
        
        if (!similarity_index.load_database(db_path)) {
            std::cerr << "Error: Failed to load database and build similarity index" << std::endl;
            return 1;
        }
        
        auto load_time = std::chrono::high_resolution_clock::now();
        
        if (verbose) {
            auto load_duration = std::chrono::duration_cast<std::chrono::milliseconds>(load_time - start_time);
            std::cout << "Index loaded in " << load_duration.count() << "ms" << std::endl;
            std::cout << "Searching for similar images..." << std::endl;
        }

        // Perform similarity search
        std::vector<SimilarImage> similar_images = similarity_index.find_similar(
            query_path, threshold, max_results);
        
        auto search_time = std::chrono::high_resolution_clock::now();
        
        if (verbose) {
            auto search_duration = std::chrono::duration_cast<std::chrono::milliseconds>(search_time - load_time);
            std::cout << "Search completed in " << search_duration.count() << "ms" << std::endl;
            std::cout << "Found " << similar_images.size() << " similar images:" << std::endl;
            std::cout << std::endl;
        }

        // Output results
        if (similar_images.empty()) {
            if (verbose) {
                std::cout << "No similar images found above threshold " << threshold << std::endl;
            }
            return 0;
        }

        // Print results - one file path per line, most similar first
        for (const auto& similar : similar_images) {
            if (verbose) {
                std::cout << similar.file_path << " (similarity: " 
                         << std::fixed << std::setprecision(3) << similar.similarity_score << ")" << std::endl;
            } else {
                std::cout << similar.file_path << std::endl;
            }
        }

        if (verbose) {
            auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(search_time - start_time);
            std::cout << std::endl << "Total time: " << total_time.count() << "ms" << std::endl;
        }

        return 0;

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}