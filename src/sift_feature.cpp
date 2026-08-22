/*
 * sift_feature.cpp - SIFT feature extraction, compact global embedding, and similarity matching
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
#include "../third_party/vlsift/vl_sift.h"
#include "../third_party/stb/stb_image.h"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>
#include <iostream>

namespace {
    constexpr uint32_t SIFT_MAGIC = 0x53494654; // 'SIFT'
    constexpr uint16_t SIFT_VERSION = 1;
    constexpr float PI_F = 3.14159265358979323846f;
}

std::vector<uint8_t> SiftFeatureData::serialize() const {
    std::vector<uint8_t> buffer;
    if (!valid) return buffer;

    uint16_t num_kpts = static_cast<uint16_t>(std::min(keypoints.size(), SiftFeatureData::MAX_KEYPOINTS));
    size_t total_size = sizeof(uint32_t) + sizeof(uint16_t) * 2 +
                        GLOBAL_DIM * sizeof(float) +
                        num_kpts * (sizeof(float) * 5 + 128);
    buffer.resize(total_size);

    uint8_t* ptr = buffer.data();

    // Magic & Version
    std::memcpy(ptr, &SIFT_MAGIC, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    std::memcpy(ptr, &SIFT_VERSION, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(ptr, &num_kpts, sizeof(uint16_t)); ptr += sizeof(uint16_t);

    // Global Vector (64 floats)
    std::memcpy(ptr, global_vector.data(), GLOBAL_DIM * sizeof(float));
    ptr += GLOBAL_DIM * sizeof(float);

    // Keypoints
    for (size_t i = 0; i < num_kpts; ++i) {
        const auto& kpt = keypoints[i];
        std::memcpy(ptr, &kpt.x, sizeof(float)); ptr += sizeof(float);
        std::memcpy(ptr, &kpt.y, sizeof(float)); ptr += sizeof(float);
        std::memcpy(ptr, &kpt.scale, sizeof(float)); ptr += sizeof(float);
        std::memcpy(ptr, &kpt.orientation, sizeof(float)); ptr += sizeof(float);
        std::memcpy(ptr, &kpt.mag, sizeof(float)); ptr += sizeof(float);
        std::memcpy(ptr, kpt.descriptor.data(), 128); ptr += 128;
    }

    return buffer;
}

bool SiftFeatureData::deserialize(const uint8_t* data, size_t size, SiftFeatureData& out) {
    out.valid = false;
    out.keypoints.clear();
    out.global_vector.fill(0.0f);

    if (!data || size < (sizeof(uint32_t) + sizeof(uint16_t) * 2 + GLOBAL_DIM * sizeof(float))) {
        return false;
    }

    const uint8_t* ptr = data;

    uint32_t magic = 0;
    uint16_t version = 0, num_kpts = 0;

    std::memcpy(&magic, ptr, sizeof(uint32_t)); ptr += sizeof(uint32_t);
    std::memcpy(&version, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);
    std::memcpy(&num_kpts, ptr, sizeof(uint16_t)); ptr += sizeof(uint16_t);

    if (magic != SIFT_MAGIC || version != SIFT_VERSION) {
        return false;
    }

    // Read global vector
    std::memcpy(out.global_vector.data(), ptr, GLOBAL_DIM * sizeof(float));
    ptr += GLOBAL_DIM * sizeof(float);

    size_t kpt_record_size = sizeof(float) * 5 + 128;
    size_t remaining_bytes = size - (ptr - data);

    if (remaining_bytes < num_kpts * kpt_record_size) {
        return false;
    }

    out.keypoints.resize(num_kpts);
    for (size_t i = 0; i < num_kpts; ++i) {
        auto& kpt = out.keypoints[i];
        std::memcpy(&kpt.x, ptr, sizeof(float)); ptr += sizeof(float);
        std::memcpy(&kpt.y, ptr, sizeof(float)); ptr += sizeof(float);
        std::memcpy(&kpt.scale, ptr, sizeof(float)); ptr += sizeof(float);
        std::memcpy(&kpt.orientation, ptr, sizeof(float)); ptr += sizeof(float);
        std::memcpy(&kpt.mag, ptr, sizeof(float)); ptr += sizeof(float);
        std::memcpy(kpt.descriptor.data(), ptr, 128); ptr += 128;
    }

    out.valid = true;
    return true;
}

bool SiftFeatureEngine::extract_from_rgb(const uint8_t* rgb_data, int width, int height, SiftFeatureData& out_data) {
    if (!rgb_data || width <= 0 || height <= 0) return false;

    // Convert RGB to grayscale
    std::vector<uint8_t> gray(width * height);
    for (int i = 0; i < width * height; ++i) {
        uint32_t r = rgb_data[i * 3 + 0];
        uint32_t g = rgb_data[i * 3 + 1];
        uint32_t b = rgb_data[i * 3 + 2];
        gray[i] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
    }

    return extract_from_gray(gray.data(), width, height, out_data);
}

bool SiftFeatureEngine::extract_from_gray(const uint8_t* gray_data, int width, int height, SiftFeatureData& out_data) {
    out_data.valid = false;
    out_data.keypoints.clear();
    out_data.global_vector.fill(0.0f);

    if (!gray_data || width <= 0 || height <= 0) return false;

    std::vector<float> im(width * height);
    for (int i = 0; i < width * height; ++i) {
        im[i] = static_cast<float>(gray_data[i]);
    }

    vl::SiftFilter filter(width, height, -1, 3, 0);
    filter.set_peak_thresh(0.04 / 3.0);
    filter.set_edge_thresh(10.0);

    std::vector<SiftPoint> all_kpts;

    int status = filter.process_first_octave(im.data());
    while (status == 0) {
        filter.detect();

        const auto& keys = filter.keypoints();
        for (const auto& k : keys) {
            double angles[4];
            int nangles = filter.calc_keypoint_orientations(angles, &k);

            for (int a = 0; a < nangles; ++a) {
                float descr[128];
                filter.calc_keypoint_descriptor(descr, &k, angles[a]);

                SiftPoint pt;
                pt.x = (width > 0) ? (k.x / static_cast<float>(width)) : 0.0f;
                pt.y = (height > 0) ? (k.y / static_cast<float>(height)) : 0.0f;
                pt.scale = k.sigma;
                pt.orientation = static_cast<float>(angles[a]);
                pt.mag = std::abs(k.s) + 0.01f;

                for (int d = 0; d < 128; ++d) {
                    float val = std::round(descr[d] * 512.0f);
                    pt.descriptor[d] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
                }
                all_kpts.push_back(pt);
            }
        }

        status = filter.process_next_octave();
    }

    // Sort keypoints by response magnitude descending and keep top MAX_KEYPOINTS
    std::sort(all_kpts.begin(), all_kpts.end(), [](const SiftPoint& a, const SiftPoint& b) {
        return a.mag > b.mag;
    });

    size_t count = std::min(all_kpts.size(), SiftFeatureData::MAX_KEYPOINTS);
    out_data.keypoints.assign(all_kpts.begin(), all_kpts.begin() + count);

    // Build the 64-D global aggregated descriptor
    build_global_vector(gray_data, width, height, out_data.keypoints, out_data.global_vector);

    out_data.valid = true;
    return true;
}

bool SiftFeatureEngine::extract_from_jpeg_bytes(const uint8_t* jpeg_data, size_t jpeg_size, SiftFeatureData& out_data) {
    if (!jpeg_data || jpeg_size == 0) return false;

    int w = 0, h = 0, channels = 0;
    unsigned char* decoded = stbi_load_from_memory(jpeg_data, static_cast<int>(jpeg_size), &w, &h, &channels, 1);
    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return false;
    }

    bool res = extract_from_gray(decoded, w, h, out_data);
    stbi_image_free(decoded);
    return res;
}

void SiftFeatureEngine::build_global_vector(const uint8_t* gray, int width, int height,
                                            const std::vector<SiftPoint>& kpts,
                                            std::array<float, SiftFeatureData::GLOBAL_DIM>& out_vec) {
    out_vec.fill(0.0f);
    if (!gray || width <= 0 || height <= 0) return;

    // 4 Spatial Quadrants (2x2 grid)
    // In each quadrant:
    // - 8 bins for keypoint descriptor orientation pooling
    // - 8 bins for direct pixel gradient orientation & intensity distribution
    // Total: 4 * 16 = 64 dimensions.

    // 1. Accumulate Keypoint features
    for (const auto& kpt : kpts) {
        int qx = (kpt.x < 0.5f) ? 0 : 1;
        int qy = (kpt.y < 0.5f) ? 0 : 1;
        int quad = qy * 2 + qx; // 0..3
        int base_idx = quad * 16;

        // Map keypoint orientation [0..2PI) into 8 bins
        float angle = kpt.orientation;
        while (angle < 0.0f) angle += 2.0f * PI_F;
        while (angle >= 2.0f * PI_F) angle -= 2.0f * PI_F;
        int ori_bin = static_cast<int>((angle / (2.0f * PI_F)) * 8.0f) % 8;

        float weight = std::max(1.0f, kpt.mag);
        out_vec[base_idx + ori_bin] += weight;

        // Pool dominant gradient components from the 128-D descriptor
        // Sum 16 sub-regions into 8 orientation sums
        for (int d = 0; d < 128; ++d) {
            int descr_ori = d % 8;
            out_vec[base_idx + 8 + descr_ori] += (kpt.descriptor[d] / 255.0f) * (weight * 0.1f);
        }
    }

    // 2. Accumulate Pixel Gradient structure (Sobel-like) across the 4 quadrants
    int hw = width / 2;
    int hh = height / 2;

    for (int y = 1; y < height - 1; ++y) {
        int qy = (y < hh) ? 0 : 1;
        for (int x = 1; x < hw - 1; ++x) {
            int qx = (x < hw) ? 0 : 1;
            int quad = qy * 2 + qx;
            int base_idx = quad * 16;

            int dx = (int)gray[y * width + (x + 1)] - (int)gray[y * width + (x - 1)];
            int dy = (int)gray[(y + 1) * width + x] - (int)gray[(y - 1) * width + x];

            float mag = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            if (mag > 5.0f) {
                float angle = std::atan2(static_cast<float>(dy), static_cast<float>(dx));
                if (angle < 0.0f) angle += 2.0f * PI_F;
                int p_bin = static_cast<int>((angle / (2.0f * PI_F)) * 8.0f) % 8;
                out_vec[base_idx + p_bin] += mag * 0.005f;
            }
        }
    }

    // 3. Power Normalization (Hellinger / signed sqrt) + L2 Normalization
    float sum_sq = 0.0f;
    for (size_t i = 0; i < SiftFeatureData::GLOBAL_DIM; ++i) {
        float v = out_vec[i];
        v = (v >= 0.0f) ? std::sqrt(v) : -std::sqrt(-v);
        out_vec[i] = v;
        sum_sq += v * v;
    }

    if (sum_sq > 1e-8f) {
        float inv_norm = 1.0f / std::sqrt(sum_sq);
        for (size_t i = 0; i < SiftFeatureData::GLOBAL_DIM; ++i) {
            out_vec[i] *= inv_norm;
        }
    }
}

float SiftFeatureEngine::compute_fast_similarity(const std::array<float, 64>& a, const std::array<float, 64>& b) {
    float dot = 0.0f;
    for (size_t i = 0; i < 64; ++i) {
        dot += a[i] * b[i];
    }
    // Clamp dot product to [0.0, 1.0]
    return std::clamp(dot, 0.0f, 1.0f);
}

float SiftFeatureEngine::compute_keypoint_similarity(const SiftFeatureData& a, const SiftFeatureData& b, float nndr_ratio) {
    if (!a.valid || !b.valid) return 0.0f;
    if (a.keypoints.empty() || b.keypoints.empty()) {
        return compute_fast_similarity(a.global_vector, b.global_vector);
    }

    const float nndr_sq = nndr_ratio * nndr_ratio;
    int matches = 0;

    for (const auto& kpt_a : a.keypoints) {
        int best_sq = std::numeric_limits<int>::max();
        int second_best_sq = std::numeric_limits<int>::max();

        const uint8_t* desc_a = kpt_a.descriptor.data();

        for (const auto& kpt_b : b.keypoints) {
            const uint8_t* desc_b = kpt_b.descriptor.data();
            int dist_sq = 0;

            #pragma unroll(8)
            for (int d = 0; d < 128; ++d) {
                int diff = static_cast<int>(desc_a[d]) - static_cast<int>(desc_b[d]);
                dist_sq += diff * diff;
            }

            if (dist_sq < best_sq) {
                second_best_sq = best_sq;
                best_sq = dist_sq;
            } else if (dist_sq < second_best_sq) {
                second_best_sq = dist_sq;
            }
        }

        if (best_sq == 0 || (second_best_sq > 0 && (static_cast<float>(best_sq) / static_cast<float>(second_best_sq)) < nndr_sq)) {
            matches++;
        }
    }

    size_t min_kpts = std::min(a.keypoints.size(), b.keypoints.size());
    float match_ratio = (min_kpts > 0) ? (static_cast<float>(matches) / static_cast<float>(min_kpts)) : 0.0f;

    // Blend keypoint match ratio with fast global similarity
    float fast_sim = compute_fast_similarity(a.global_vector, b.global_vector);
    float final_score = std::clamp(match_ratio * 0.7f + fast_sim * 0.3f, 0.0f, 1.0f);
    return final_score;
}

float SiftFeatureEngine::compute_similarity(const SiftFeatureData& a, const SiftFeatureData& b, bool high_accuracy) {
    if (high_accuracy) {
        return compute_keypoint_similarity(a, b);
    }
    return compute_fast_similarity(a.global_vector, b.global_vector);
}
