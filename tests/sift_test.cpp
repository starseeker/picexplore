/*
 * sift_test.cpp - Test suite and benchmark harness for SIFT similarity matching
 *
 * Copyright (c) 2026 Clifford Yapp
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

#include "sift_feature.h"
#include "database.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <random>
#include <iomanip>
#include <filesystem>

namespace fs = std::filesystem;

// Generate synthetic test image patterns (256x256 RGB)
std::vector<uint8_t> generate_pattern(int width, int height, int type, float param = 0.0f) {
    std::vector<uint8_t> rgb(width * height * 3, 0);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = (y * width + x) * 3;
            uint8_t r = 0, g = 0, b = 0;

            switch (type) {
                case 0: // Horizontal stripes / grid
                    r = ((x / 16 + y / 16) % 2 == 0) ? 255 : 0;
                    g = r; b = r;
                    break;
                case 1: // Diagonal sinusoidal waves
                    r = static_cast<uint8_t>(127.5f + 127.5f * std::sin((x + y + param * 20.0f) * 0.1f));
                    g = static_cast<uint8_t>(127.5f + 127.5f * std::cos((x * 0.15f)));
                    b = static_cast<uint8_t>(127.5f + 127.5f * std::sin((y * 0.15f)));
                    break;
                case 2: // Concentric rings
                    {
                        float dx = x - width / 2.0f;
                        float dy = y - height / 2.0f;
                        float dist = std::sqrt(dx * dx + dy * dy);
                        r = static_cast<uint8_t>(127.5f + 127.5f * std::sin(dist * 0.2f + param));
                        g = r; b = r;
                    }
                    break;
                case 3: // Corner feature points / stars
                    {
                        int kx = (x % 32) - 16;
                        int ky = (y % 32) - 16;
                        float d = std::sqrt(static_cast<float>(kx * kx + ky * ky));
                        r = (d < 6.0f) ? 255 : 30;
                        g = (d < 4.0f) ? 200 : 20;
                        b = (d < 2.0f) ? 255 : 10;
                    }
                    break;
                case 4: // Uniform gradient
                    r = static_cast<uint8_t>((x * 255) / width);
                    g = static_cast<uint8_t>((y * 255) / height);
                    b = 128;
                    break;
                default:
                    r = g = b = 128;
                    break;
            }
            rgb[idx + 0] = r;
            rgb[idx + 1] = g;
            rgb[idx + 2] = b;
        }
    }
    return rgb;
}

void test_sift_serialization() {
    std::cout << "--- Testing SIFT Feature Serialization ---" << std::endl;

    auto img = generate_pattern(256, 256, 3);
    SiftFeatureData data1;
    bool ok = SiftFeatureEngine::extract_from_rgb(img.data(), 256, 256, data1);
    assert(ok && "SIFT extraction failed");
    assert(data1.valid && "Extracted SIFT data marked invalid");
    assert(!data1.keypoints.empty() && "No keypoints detected for corner pattern");

    std::cout << "  Extracted " << data1.keypoints.size() << " keypoints from 256x256 image." << std::endl;

    std::vector<uint8_t> bytes = data1.serialize();
    assert(!bytes.empty() && "Serialization produced empty byte vector");

    SiftFeatureData data2;
    ok = SiftFeatureData::deserialize(bytes.data(), bytes.size(), data2);
    assert(ok && "Deserialization failed");
    assert(data2.valid && "Deserialized data marked invalid");
    assert(data1.keypoints.size() == data2.keypoints.size() && "Keypoint count mismatch");

    for (size_t i = 0; i < SiftFeatureData::GLOBAL_DIM; ++i) {
        assert(std::abs(data1.global_vector[i] - data2.global_vector[i]) < 1e-5f && "Global vector mismatch");
    }

    std::cout << "  Serialization / Deserialization: PASSED (Payload size: " << bytes.size() << " bytes)" << std::endl;
}

void test_similarity_consistency() {
    std::cout << "--- Testing Similarity Scoring Consistency ---" << std::endl;

    auto img_a1 = generate_pattern(256, 256, 1, 0.0f);
    auto img_a2 = generate_pattern(256, 256, 1, 0.05f); // slightly shifted version of pattern 1
    auto img_b  = generate_pattern(256, 256, 0);       // checkerboard pattern
    auto img_c  = generate_pattern(256, 256, 2);       // concentric rings

    SiftFeatureData feat_a1, feat_a2, feat_b, feat_c;
    SiftFeatureEngine::extract_from_rgb(img_a1.data(), 256, 256, feat_a1);
    SiftFeatureEngine::extract_from_rgb(img_a2.data(), 256, 256, feat_a2);
    SiftFeatureEngine::extract_from_rgb(img_b.data(), 256, 256, feat_b);
    SiftFeatureEngine::extract_from_rgb(img_c.data(), 256, 256, feat_c);

    // Self-similarity should be 1.0
    float sim_self_fast = SiftFeatureEngine::compute_fast_similarity(feat_a1.global_vector, feat_a1.global_vector);
    float sim_self_acc  = SiftFeatureEngine::compute_keypoint_similarity(feat_a1, feat_a1);

    std::cout << "  Self-similarity (Fast): " << sim_self_fast << " (Expected ~1.0)" << std::endl;
    std::cout << "  Self-similarity (High Accuracy): " << sim_self_acc << " (Expected ~1.0)" << std::endl;
    assert(sim_self_fast > 0.99f);
    assert(sim_self_acc > 0.95f);

    // Near duplicate (A1 vs A2) should be higher than dissimilar (A1 vs B)
    float sim_a1_a2_fast = SiftFeatureEngine::compute_fast_similarity(feat_a1.global_vector, feat_a2.global_vector);
    float sim_a1_b_fast  = SiftFeatureEngine::compute_fast_similarity(feat_a1.global_vector, feat_b.global_vector);

    float sim_a1_a2_acc  = SiftFeatureEngine::compute_keypoint_similarity(feat_a1, feat_a2);
    float sim_a1_b_acc   = SiftFeatureEngine::compute_keypoint_similarity(feat_a1, feat_b);

    std::cout << "  Similar pair (A1 vs A2) Fast: " << sim_a1_a2_fast << ", High Acc: " << sim_a1_a2_acc << std::endl;
    std::cout << "  Dissimilar pair (A1 vs B) Fast: " << sim_a1_b_fast << ", High Acc: " << sim_a1_b_acc << std::endl;

    assert(sim_a1_a2_fast > sim_a1_b_fast && "Fast similarity ranking violated");
    assert(sim_a1_a2_acc > sim_a1_b_acc && "High accuracy similarity ranking violated");

    std::cout << "  Similarity consistency tests: PASSED" << std::endl;
}

// Compute Spearman's Rank Correlation Coefficient between two score vectors
double compute_spearman_correlation(const std::vector<float>& scores1, const std::vector<float>& scores2) {
    size_t n = scores1.size();
    if (n <= 1) return 1.0;

    std::vector<size_t> idx1(n), idx2(n);
    std::iota(idx1.begin(), idx1.end(), 0);
    std::iota(idx2.begin(), idx2.end(), 0);

    std::sort(idx1.begin(), idx1.end(), [&scores1](size_t a, size_t b) { return scores1[a] > scores1[b]; });
    std::sort(idx2.begin(), idx2.end(), [&scores2](size_t a, size_t b) { return scores2[a] > scores2[b]; });

    std::vector<double> rank1(n), rank2(n);
    for (size_t i = 0; i < n; ++i) {
        rank1[idx1[i]] = static_cast<double>(i);
        rank2[idx2[i]] = static_cast<double>(i);
    }

    double sum_d_sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double d = rank1[i] - rank2[i];
        sum_d_sq += d * d;
    }

    double rho = 1.0 - (6.0 * sum_d_sq) / (static_cast<double>(n) * (static_cast<double>(n * n) - 1.0));
    return rho;
}

// Compute Top-K overlap (Intersection over K)
double compute_top_k_overlap(const std::vector<float>& scores1, const std::vector<float>& scores2, size_t K) {
    size_t n = scores1.size();
    K = std::min(K, n);
    if (K == 0) return 1.0;

    std::vector<size_t> idx1(n), idx2(n);
    std::iota(idx1.begin(), idx1.end(), 0);
    std::iota(idx2.begin(), idx2.end(), 0);

    std::sort(idx1.begin(), idx1.end(), [&scores1](size_t a, size_t b) { return scores1[a] > scores1[b]; });
    std::sort(idx2.begin(), idx2.end(), [&scores2](size_t a, size_t b) { return scores2[a] > scores2[b]; });

    std::vector<size_t> top1(idx1.begin(), idx1.begin() + K);
    std::vector<size_t> top2(idx2.begin(), idx2.begin() + K);

    std::sort(top1.begin(), top1.end());
    std::sort(top2.begin(), top2.end());

    std::vector<size_t> common;
    std::set_intersection(top1.begin(), top1.end(), top2.begin(), top2.end(), std::back_inserter(common));

    return static_cast<double>(common.size()) / static_cast<double>(K);
}

void benchmark_and_compare(size_t num_images = 100) {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  SIFT Benchmark & Comparison (Fast vs. High Accuracy)  " << std::endl;
    std::cout << "========================================================" << std::endl;

    std::cout << "Generating " << num_images << " synthetic test images..." << std::endl;
    std::vector<SiftFeatureData> dataset(num_images);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_images; ++i) {
        int pattern_type = static_cast<int>(i % 5);
        float param = static_cast<float>(i / 5) * 0.1f;
        auto img = generate_pattern(256, 256, pattern_type, param);
        SiftFeatureEngine::extract_from_rgb(img.data(), 256, 256, dataset[i]);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double extraction_total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Feature Extraction Time: " << extraction_total_ms << " ms total ("
              << (extraction_total_ms / num_images) << " ms/image)\n" << std::endl;

    // Pick query image (index 0)
    const auto& query = dataset[0];

    // Benchmark Fast Mode
    std::vector<float> fast_scores(num_images);
    auto t_fast_0 = std::chrono::high_resolution_clock::now();
    for (size_t rep = 0; rep < 100; ++rep) {
        for (size_t i = 0; i < num_images; ++i) {
            fast_scores[i] = SiftFeatureEngine::compute_fast_similarity(query.global_vector, dataset[i].global_vector);
        }
    }
    auto t_fast_1 = std::chrono::high_resolution_clock::now();
    double fast_query_time_us = std::chrono::duration<double, std::micro>(t_fast_1 - t_fast_0).count() / (100.0 * num_images);

    // Benchmark High Accuracy Mode
    std::vector<float> acc_scores(num_images);
    auto t_acc_0 = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < num_images; ++i) {
        acc_scores[i] = SiftFeatureEngine::compute_keypoint_similarity(query, dataset[i]);
    }
    auto t_acc_1 = std::chrono::high_resolution_clock::now();
    double acc_query_time_us = std::chrono::duration<double, std::micro>(t_acc_1 - t_acc_0).count() / num_images;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Query Comparison Speed per Image Pair:" << std::endl;
    std::cout << "  - Fast (Global Vector):        " << fast_query_time_us << " \u03bcs / comparison (~"
              << (fast_query_time_us * 50000.0 / 1000.0) << " ms for 50,000 images)" << std::endl;
    std::cout << "  - High Accuracy (Keypoints):   " << acc_query_time_us << " \u03bcs / comparison (~"
              << (acc_query_time_us * 50000.0 / 1000.0 / 8.0) << " ms for 50,000 images on 8 threads)" << std::endl;

    // Compare Ranking Agreement
    double spearman_rho = compute_spearman_correlation(fast_scores, acc_scores);
    double top_5_overlap = compute_top_k_overlap(fast_scores, acc_scores, 5);
    double top_10_overlap = compute_top_k_overlap(fast_scores, acc_scores, 10);
    double top_20_overlap = compute_top_k_overlap(fast_scores, acc_scores, 20);

    std::cout << "\nRanking Correlation & Agreement (Fast vs. High Accuracy):" << std::endl;
    std::cout << "  - Spearman Rank Correlation \u03c1: " << spearman_rho << " (1.0 = identical ranking)" << std::endl;
    std::cout << "  - Top-5  Candidate Overlap:   " << (top_5_overlap * 100.0) << " %" << std::endl;
    std::cout << "  - Top-10 Candidate Overlap:   " << (top_10_overlap * 100.0) << " %" << std::endl;
    std::cout << "  - Top-20 Candidate Overlap:   " << (top_20_overlap * 100.0) << " %" << std::endl;

    // Display Top 5 Rankings Side-by-Side
    std::vector<size_t> rank_fast(num_images), rank_acc(num_images);
    std::iota(rank_fast.begin(), rank_fast.end(), 0);
    std::iota(rank_acc.begin(), rank_acc.end(), 0);

    std::sort(rank_fast.begin(), rank_fast.end(), [&fast_scores](size_t a, size_t b) { return fast_scores[a] > fast_scores[b]; });
    std::sort(rank_acc.begin(), rank_acc.end(), [&acc_scores](size_t a, size_t b) { return acc_scores[a] > acc_scores[b]; });

    std::cout << "\nTop 5 Ranked Items Comparison:" << std::endl;
    std::cout << "  Rank | Fast Mode (Index : Score) | High Accuracy (Index : Score)" << std::endl;
    std::cout << "  -----+---------------------------+------------------------------" << std::endl;
    for (size_t r = 0; r < std::min(size_t(5), num_images); ++r) {
        size_t ifast = rank_fast[r];
        size_t iacc  = rank_acc[r];
        std::cout << "    " << (r + 1) << "  |   #" << std::setw(3) << ifast << " : " << std::setw(6) << (fast_scores[ifast] * 100.0f) << "%   |   #"
                  << std::setw(3) << iacc << " : " << std::setw(6) << (acc_scores[iacc] * 100.0f) << "%" << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;
}

void benchmark_real_cache(const std::string& db_path, size_t max_images = 60) {
    if (!fs::exists(db_path)) return;

    std::cout << "\n========================================================" << std::endl;
    std::cout << "  SIFT Evaluation on Real Cache Images (" << db_path << ")" << std::endl;
    std::cout << "========================================================" << std::endl;

    DatabaseManager db;
    if (!db.open(db_path)) {
        std::cout << "Could not open database at " << db_path << std::endl;
        return;
    }

    std::vector<std::pair<std::string, std::string>> path_and_hashes;
    MDB_txn* txn = nullptr;
    if (mdb_txn_begin(db.get_env(), nullptr, MDB_RDONLY, &txn) == 0) {
        MDB_dbi dbi;
        if (mdb_dbi_open(txn, nullptr, 0, &dbi) == 0) {
            MDB_cursor* cursor = nullptr;
            if (mdb_cursor_open(txn, dbi, &cursor) == 0) {
                std::string prefix = "file:";
                MDB_val key, data;
                key.mv_data = (void*)prefix.c_str();
                key.mv_size = prefix.length();

                int rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
                while (rc == 0 && path_and_hashes.size() < max_images) {
                    std::string k((const char*)key.mv_data, key.mv_size);
                    if (k.rfind("file:", 0) != 0) break;
                    std::string v((const char*)data.mv_data, data.mv_size);
                    std::string filepath = k.substr(5);
                    path_and_hashes.push_back({filepath, v});
                    rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
                }
                mdb_cursor_close(cursor);
            }
        }
        mdb_txn_abort(txn);
    }

    if (path_and_hashes.empty()) {
        std::cout << "No file entries found in database." << std::endl;
        return;
    }

    std::cout << "Evaluating on " << path_and_hashes.size() << " cached images..." << std::endl;

    std::vector<SiftFeatureData> features;
    features.reserve(path_and_hashes.size());
    std::vector<std::string> paths;
    paths.reserve(path_and_hashes.size());

    auto t0 = std::chrono::high_resolution_clock::now();
    for (const auto& [p, h] : path_and_hashes) {
        SiftFeatureData feat;
        if (db.extract_sift_from_cached_thumbnail(h, feat)) {
            features.push_back(feat);
            paths.push_back(p);
        }
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double extract_time = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (features.empty()) {
        std::cout << "Could not extract SIFT features from cached thumbnails." << std::endl;
        return;
    }

    std::cout << "Extracted/loaded SIFT for " << features.size() << " images in " << extract_time << " ms ("
              << (extract_time / features.size()) << " ms/image)" << std::endl;

    // Run 3 query comparisons with different query images
    size_t num_queries = std::min(size_t(3), features.size());
    for (size_t q = 0; q < num_queries; ++q) {
        size_t query_idx = (q * (features.size() / num_queries));
        const auto& query = features[query_idx];
        std::string query_filename = fs::path(paths[query_idx]).filename().string();

        std::vector<float> fast_scores(features.size());
        std::vector<float> acc_scores(features.size());

        for (size_t i = 0; i < features.size(); ++i) {
            fast_scores[i] = SiftFeatureEngine::compute_fast_similarity(query.global_vector, features[i].global_vector);
            acc_scores[i] = SiftFeatureEngine::compute_keypoint_similarity(query, features[i]);
        }

        double rho = compute_spearman_correlation(fast_scores, acc_scores);
        double top_5 = compute_top_k_overlap(fast_scores, acc_scores, 5);
        double top_10 = compute_top_k_overlap(fast_scores, acc_scores, 10);

        std::cout << "\nQuery #" << (q + 1) << " [" << query_filename << "]:" << std::endl;
        std::cout << "  - Spearman Correlation \u03c1: " << std::fixed << std::setprecision(3) << rho << std::endl;
        std::cout << "  - Top-5  Candidate Overlap:   " << (top_5 * 100.0) << " %" << std::endl;
        std::cout << "  - Top-10 Candidate Overlap:   " << (top_10 * 100.0) << " %" << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;
}

void compare_thumbnail_sizes() {
    std::cout << "\n========================================================" << std::endl;
    std::cout << "  Multi-Resolution Scaling Benchmark (128, 256, 512, 1024, 2048)" << std::endl;
    std::cout << "========================================================" << std::endl;

    std::vector<int> test_sizes = {128, 256, 512, 1024, 2048};
    std::cout << std::left << std::setw(16) << "Resolution"
              << std::setw(18) << "Extraction Time"
              << std::setw(18) << "Avg Keypoints"
              << std::setw(18) << "Relative Pixels"
              << std::setw(18) << "50k Scan Impact" << std::endl;
    std::cout << std::string(88, '-') << std::endl;

    for (int sz : test_sizes) {
        double total_ms = 0.0;
        size_t total_kpts = 0;
        int trials = (sz >= 1024) ? 5 : 10;

        for (int t = 0; t < trials; ++t) {
            auto img = generate_pattern(sz, sz, 3, static_cast<float>(t));
            auto t0 = std::chrono::high_resolution_clock::now();
            SiftFeatureData feat;
            SiftFeatureEngine::extract_from_rgb(img.data(), sz, sz, feat);
            auto t1 = std::chrono::high_resolution_clock::now();
            total_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            total_kpts += feat.keypoints.size();
        }

        double avg_ms = total_ms / trials;
        double avg_kpts = static_cast<double>(total_kpts) / trials;
        double rel_pix = static_cast<double>(sz * sz) / (256.0 * 256.0);
        double scan_hrs = (avg_ms * 50000.0) / (1000.0 * 3600.0 * 8.0); // 8 threads

        std::cout << std::left << std::setw(16) << (std::to_string(sz) + "x" + std::to_string(sz))
                  << std::setw(18) << (std::to_string(avg_ms).substr(0, 6) + " ms")
                  << std::setw(18) << (std::to_string(avg_kpts).substr(0, 5) + " kpts")
                  << std::setw(18) << (std::to_string(rel_pix).substr(0, 4) + "x")
                  << std::setw(18) << (std::to_string(scan_hrs * 60.0).substr(0, 5) + " min") << std::endl;
    }
    std::cout << "========================================================\n" << std::endl;
}

int main(int argc, char** argv) {
    std::cout << "=== Running PicExplore SIFT Similarity Tests ===" << std::endl;

    test_sift_serialization();
    test_similarity_consistency();
    benchmark_and_compare(100);

    std::string home_db = (fs::path(getenv("HOME") ? getenv("HOME") : ".") / ".cache" / "picexplore" / "cache.db").string();
    benchmark_real_cache(home_db, 60);
    compare_thumbnail_sizes();

    std::cout << "All SIFT tests completed successfully!" << std::endl;
    return 0;
}
