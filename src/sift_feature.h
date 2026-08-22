/*
 * sift_feature.h - SIFT feature extraction, compact global embedding, and similarity matching
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

#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <cstddef>

struct SiftPoint {
    float x = 0.0f;           // Normalized [0..1] x coordinate
    float y = 0.0f;           // Normalized [0..1] y coordinate
    float scale = 0.0f;       // Keypoint scale
    float orientation = 0.0f; // Orientation in radians
    float mag = 0.0f;         // Response magnitude
    std::array<uint8_t, 128> descriptor{}; // 128-D integer descriptor (0..255)
};

struct SiftFeatureData {
    static constexpr size_t GLOBAL_DIM = 64;
    static constexpr size_t MAX_KEYPOINTS = 48;

    std::array<float, GLOBAL_DIM> global_vector{}; // L2-normalized 64-D global descriptor
    std::vector<SiftPoint> keypoints;             // Salient keypoints
    bool valid = false;

    // Serialization for LMDB storage (<hash>:sift)
    std::vector<uint8_t> serialize() const;
    static bool deserialize(const uint8_t* data, size_t size, SiftFeatureData& out);
};

class SiftFeatureEngine {
public:
    // Extract SIFT features from RGB / Grayscale pixel buffers (e.g. 256x256 thumbnail)
    static bool extract_from_rgb(const uint8_t* rgb_data, int width, int height, SiftFeatureData& out_data);
    static bool extract_from_gray(const uint8_t* gray_data, int width, int height, SiftFeatureData& out_data);
    static bool extract_from_jpeg_bytes(const uint8_t* jpeg_data, size_t jpeg_size, SiftFeatureData& out_data);

    // Fast Mode: Cosine similarity between 64-D global embeddings -> returns [0.0, 1.0]
    static float compute_fast_similarity(const std::array<float, 64>& a, const std::array<float, 64>& b);

    // High Accuracy Mode: Lowe's ratio test on keypoint descriptors -> returns [0.0, 1.0]
    static float compute_keypoint_similarity(const SiftFeatureData& a, const SiftFeatureData& b, float nndr_ratio = 0.75f);

    // Combined score (optional blend)
    static float compute_similarity(const SiftFeatureData& a, const SiftFeatureData& b, bool high_accuracy = false);

private:
    static void build_global_vector(const uint8_t* gray, int width, int height,
                                   const std::vector<SiftPoint>& kpts,
                                   std::array<float, SiftFeatureData::GLOBAL_DIM>& out_vec);
};
