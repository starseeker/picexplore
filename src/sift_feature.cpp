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
    constexpr uint16_t SIFT_VERSION = 4;
    constexpr float PI_F = 3.14159265358979323846f;

    bool extract_keypoints_and_global(const uint8_t* gray_data, const uint8_t* rgb_data,
                                      int width, int height, SiftFeatureData& out_data) {
        out_data.valid = false;
        out_data.keypoints.clear();
        out_data.global_vector.fill(0.0f);

        if (!gray_data || width <= 0 || height <= 0) return false;

        const int num_pixels = width * height;
        int hist[256] = {0};
        for (int i = 0; i < num_pixels; ++i) {
            hist[gray_data[i]]++;
        }

        // Use 0.1% and 99.9% quantiles so sparse line drawings (where ink or
        // blueprint lines cover <1% of pixels) are never clipped into background.
        int target_lo = std::max(1, num_pixels / 1000);
        int target_hi = std::max(1, (num_pixels * 999) / 1000);
        int cum = 0, p_lo = 0, p_hi = 255;
        for (int v = 0; v < 256; ++v) {
            cum += hist[v];
            if (cum >= target_lo) { p_lo = v; break; }
        }
        cum = 0;
        for (int v = 0; v < 256; ++v) {
            cum += hist[v];
            if (cum >= target_hi) { p_hi = v; break; }
        }

        std::vector<float> im(num_pixels);
        if (p_hi - p_lo >= 8) {
            float scale = 255.0f / static_cast<float>(p_hi - p_lo);
            for (int i = 0; i < num_pixels; ++i) {
                float stretched = (static_cast<float>(gray_data[i]) - static_cast<float>(p_lo)) * scale;
                im[i] = std::clamp(stretched, 0.0f, 255.0f);
            }
        } else {
            for (int i = 0; i < num_pixels; ++i) {
                im[i] = static_cast<float>(gray_data[i]);
            }
        }

        vl::SiftFilter filter(width, height, -1, 3, 0);
        // im is in [0..255]; 1.6 corresponds to ~0.019 / 3 on normalized [0..1]
        filter.set_peak_thresh(1.6);
        filter.set_edge_thresh(10.0);

        std::vector<SiftPoint> all_kpts;
        const float max_dim = static_cast<float>(std::max(width, height));

        int status = filter.process_first_octave(im.data());
        while (status == 0) {
            filter.detect();

            const auto& keys = filter.keypoints();
            std::vector<const vl::SiftKeypoint*> octave_keys;
            octave_keys.reserve(keys.size());
            for (const auto& k : keys) {
                octave_keys.push_back(&k);
            }
            std::sort(octave_keys.begin(), octave_keys.end(), [](const vl::SiftKeypoint* a, const vl::SiftKeypoint* b) {
                float ma = a->contrast * std::sqrt(std::clamp(a->sigma, 1.6f, 12.0f));
                float mb = b->contrast * std::sqrt(std::clamp(b->sigma, 1.6f, 12.0f));
                return ma > mb;
            });

            int octave_cell_counts[16] = {0};
            for (const auto* kp : octave_keys) {
                const auto& k = *kp;
                float nx = (width > 0) ? (k.x / static_cast<float>(width)) : 0.0f;
                float ny = (height > 0) ? (k.y / static_cast<float>(height)) : 0.0f;
                int cx = std::clamp(static_cast<int>(nx * 4.0f), 0, 3);
                int cy = std::clamp(static_cast<int>(ny * 4.0f), 0, 3);
                int cell_idx = cy * 4 + cx;
                if (octave_cell_counts[cell_idx] >= 12) continue;
                octave_cell_counts[cell_idx]++;

                double angles[4];
                int nangles = filter.calc_keypoint_orientations(angles, &k);
                int max_ori = std::min(nangles, 2);

                for (int a = 0; a < max_ori; ++a) {
                    float descr[128];
                    filter.calc_keypoint_descriptor(descr, &k, angles[a]);

                    SiftPoint pt;
                    pt.x = nx;
                    pt.y = ny;
                    pt.scale = (max_dim > 0.0f) ? (k.sigma / max_dim) : k.sigma;
                    pt.orientation = static_cast<float>(angles[a]);
                    pt.mag = k.contrast * std::sqrt(std::clamp(k.sigma, 1.6f, 12.0f));

                    int desc_sum = 0;
                    for (int d = 0; d < 128; ++d) {
                        float val = std::round(descr[d] * 512.0f);
                        uint8_t qv = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
                        pt.descriptor[d] = qv;
                        desc_sum += qv;
                    }
                    if (desc_sum > 0) {
                        all_kpts.push_back(pt);
                    }
                }
            }

            status = filter.process_next_octave();
        }

        // Sort keypoints by salience descending
        std::sort(all_kpts.begin(), all_kpts.end(), [](const SiftPoint& a, const SiftPoint& b) {
            return a.mag > b.mag;
        });

        // Select up to MAX_KEYPOINTS using 4x4 spatial cell round-robin so dense
        // corner text blocks cannot monopolize all keypoints.
        if (all_kpts.size() <= SiftFeatureData::MAX_KEYPOINTS) {
            out_data.keypoints = std::move(all_kpts);
        } else {
            std::vector<const SiftPoint*> cell_buckets[16];
            for (const auto& pt : all_kpts) {
                int cx = std::clamp(static_cast<int>(pt.x * 4.0f), 0, 3);
                int cy = std::clamp(static_cast<int>(pt.y * 4.0f), 0, 3);
                auto& bucket = cell_buckets[cy * 4 + cx];
                bool near_dup = false;
                for (const auto* existing : bucket) {
                    float dx = pt.x - existing->x;
                    float dy = pt.y - existing->y;
                    if (dx * dx + dy * dy < 0.0004f) {
                        float dori = std::abs(pt.orientation - existing->orientation);
                        if (dori > PI_F) dori = 2.0f * PI_F - dori;
                        if (dori < 0.35f) {
                            near_dup = true;
                            break;
                        }
                    }
                }
                if (!near_dup) {
                    bucket.push_back(&pt);
                }
            }

            size_t cell_cursor[16] = {0};
            out_data.keypoints.reserve(SiftFeatureData::MAX_KEYPOINTS);
            while (out_data.keypoints.size() < SiftFeatureData::MAX_KEYPOINTS) {
                int best_cell = -1;
                float best_mag = -1.0f;
                // Pick the non-empty cell at the current round level with highest salience
                size_t min_round = std::numeric_limits<size_t>::max();
                for (int c = 0; c < 16; ++c) {
                    if (cell_cursor[c] < cell_buckets[c].size()) {
                        min_round = std::min(min_round, cell_cursor[c]);
                    }
                }
                if (min_round == std::numeric_limits<size_t>::max()) break;

                for (int c = 0; c < 16; ++c) {
                    if (cell_cursor[c] == min_round && cell_cursor[c] < cell_buckets[c].size()) {
                        float m = cell_buckets[c][cell_cursor[c]]->mag;
                        if (m > best_mag) {
                            best_mag = m;
                            best_cell = c;
                        }
                    }
                }
                if (best_cell < 0) break;
                out_data.keypoints.push_back(*cell_buckets[best_cell][cell_cursor[best_cell]]);
                cell_cursor[best_cell]++;
            }

            // If deduplication left fewer than MAX_KEYPOINTS, top up from remaining all_kpts
            if (out_data.keypoints.size() < SiftFeatureData::MAX_KEYPOINTS) {
                for (const auto& pt : all_kpts) {
                    if (out_data.keypoints.size() >= SiftFeatureData::MAX_KEYPOINTS) break;
                    bool already = false;
                    for (const auto& sel : out_data.keypoints) {
                        if (sel.x == pt.x && sel.y == pt.y && sel.orientation == pt.orientation) {
                            already = true;
                            break;
                        }
                    }
                    if (!already) out_data.keypoints.push_back(pt);
                }
            }
        }

        out_data.valid = true;
        return true;
    }
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

    std::vector<uint8_t> gray(width * height);
    for (int i = 0; i < width * height; ++i) {
        uint32_t r = rgb_data[i * 3 + 0];
        uint32_t g = rgb_data[i * 3 + 1];
        uint32_t b = rgb_data[i * 3 + 2];
        gray[i] = static_cast<uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
    }

    if (!extract_keypoints_and_global(gray.data(), rgb_data, width, height, out_data)) {
        return false;
    }
    build_global_vector(gray.data(), rgb_data, width, height, out_data.keypoints, out_data.global_vector);
    return true;
}

bool SiftFeatureEngine::extract_from_gray(const uint8_t* gray_data, int width, int height, SiftFeatureData& out_data) {
    if (!extract_keypoints_and_global(gray_data, nullptr, width, height, out_data)) {
        return false;
    }
    build_global_vector(gray_data, nullptr, width, height, out_data.keypoints, out_data.global_vector);
    return true;
}

bool SiftFeatureEngine::extract_from_jpeg_bytes(const uint8_t* jpeg_data, size_t jpeg_size, SiftFeatureData& out_data) {
    if (!jpeg_data || jpeg_size == 0) return false;

    int w = 0, h = 0, channels = 0;
    unsigned char* decoded = stbi_load_from_memory(jpeg_data, static_cast<int>(jpeg_size), &w, &h, &channels, 3);
    if (!decoded || w <= 0 || h <= 0) {
        if (decoded) stbi_image_free(decoded);
        return false;
    }

    bool res = extract_from_rgb(decoded, w, h, out_data);
    stbi_image_free(decoded);
    return res;
}

void SiftFeatureEngine::build_global_vector(const uint8_t* gray, const uint8_t* rgb, int width, int height,
                                            const std::vector<SiftPoint>& kpts,
                                            std::array<float, SiftFeatureData::GLOBAL_DIM>& out_vec) {
    out_vec.fill(0.0f);
    if (!gray || width <= 0 || height <= 0) return;

    const int num_pixels = width * height;

    // Global and per-quadrant luminance and opponent color statistics
    double sum_l = 0.0, sum_l2 = 0.0;
    double sum_rb = 0.0, sum_gm = 0.0, sum_sat = 0.0;
    float cell_lum_sum[16] = {0.0f};
    float cell_rb_sum[16] = {0.0f};
    float cell_gm_sum[16] = {0.0f};
    int cell_pix_cnt[16] = {0};

    for (int y = 0; y < height; ++y) {
        int cy = std::min(3, (y * 4) / std::max(1, height));
        for (int x = 0; x < width; ++x) {
            int cx = std::min(3, (x * 4) / std::max(1, width));
            int cell = cy * 4 + cx;
            int idx = y * width + x;
            float l = static_cast<float>(gray[idx]) * (1.0f / 255.0f);
            sum_l += l;
            sum_l2 += l * l;
            cell_lum_sum[cell] += l;
            cell_pix_cnt[cell]++;

            if (rgb) {
                float r = static_cast<float>(rgb[idx * 3 + 0]) * (1.0f / 255.0f);
                float g = static_cast<float>(rgb[idx * 3 + 1]) * (1.0f / 255.0f);
                float b = static_cast<float>(rgb[idx * 3 + 2]) * (1.0f / 255.0f);
                float rb = r - b;
                float gm = (2.0f * g - r - b) * 0.5f;
                sum_rb += rb;
                sum_gm += gm;
                cell_rb_sum[cell] += rb;
                cell_gm_sum[cell] += gm;
                float mx = std::max({r, g, b});
                float mn = std::min({r, g, b});
                sum_sat += (mx - mn);
            }
        }
    }

    float mean_l = static_cast<float>(sum_l / std::max(1, num_pixels));
    float var_l = std::max(0.0f, static_cast<float>(sum_l2 / std::max(1, num_pixels)) - mean_l * mean_l);
    float std_l = std::sqrt(var_l);
    float mean_rb = static_cast<float>(sum_rb / std::max(1, num_pixels));
    float mean_gm = static_cast<float>(sum_gm / std::max(1, num_pixels));
    float mean_sat = static_cast<float>(sum_sat / std::max(1, num_pixels));

    float cell_lum[16] = {0.0f};
    float cell_rb[16] = {0.0f};
    float cell_gm[16] = {0.0f};
    for (int c = 0; c < 16; ++c) {
        float inv_c = (cell_pix_cnt[c] > 0) ? (1.0f / cell_pix_cnt[c]) : 0.0f;
        cell_lum[c] = (cell_pix_cnt[c] > 0) ? (cell_lum_sum[c] * inv_c) : mean_l;
        cell_rb[c]  = cell_rb_sum[c] * inv_c;
        cell_gm[c]  = cell_gm_sum[c] * inv_c;
    }

    // 4x4 grid edge energy and double-angle structure tensor components
    float cell_hv[16] = {0.0f};
    float cell_diag[16] = {0.0f};
    float cell_c2[16] = {0.0f};
    float cell_s2[16] = {0.0f};
    float cell_edge[16] = {0.0f};
    int cell_edge_cnt[16] = {0};

    for (int y = 1; y < height - 1; ++y) {
        int cy = std::min(3, (y * 4) / std::max(1, height));
        for (int x = 1; x < width - 1; ++x) {
            int cx = std::min(3, (x * 4) / std::max(1, width));
            int cell = cy * 4 + cx;

            float dx = static_cast<float>(gray[y * width + (x + 1)]) - static_cast<float>(gray[y * width + (x - 1)]);
            float dy = static_cast<float>(gray[(y + 1) * width + x]) - static_cast<float>(gray[(y - 1) * width + x]);
            float mag_sq = dx * dx + dy * dy;
            cell_edge_cnt[cell]++;

            if (mag_sq > 16.0f) {
                float mag = std::sqrt(mag_sq);
                // Double-angle orientation components:
                // c2 = mag * cos(2*theta) (+mag for vertical edges, -mag for horizontal edges)
                // s2 = mag * sin(2*theta) (+mag for +45 deg edges, -mag for -45 deg edges)
                float c2 = (dx * dx - dy * dy) / mag;
                float s2 = (2.0f * dx * dy) / mag;

                cell_hv[cell] += 0.5f * (mag + std::abs(c2));
                cell_diag[cell] += 0.5f * (mag + std::abs(s2));
                cell_c2[cell] += c2;
                cell_s2[cell] += s2;
                cell_edge[cell] += mag;
            }
        }
    }

    // Fold SIFT keypoint spatial/orientation energy into the 4x4 cells
    for (const auto& kpt : kpts) {
        int cx = std::clamp(static_cast<int>(kpt.x * 4.0f), 0, 3);
        int cy = std::clamp(static_cast<int>(kpt.y * 4.0f), 0, 3);
        int cell = cy * 4 + cx;
        float w = std::clamp(kpt.mag * 0.1f, 0.5f, 8.0f);
        float c2 = w * std::cos(2.0f * kpt.orientation);
        float s2 = w * std::sin(2.0f * kpt.orientation);
        float scale_cnt = static_cast<float>(std::max(1, cell_edge_cnt[cell])) * 0.02f;
        cell_hv[cell] += 0.5f * (w + std::abs(c2)) * scale_cnt;
        cell_diag[cell] += 0.5f * (w + std::abs(s2)) * scale_cnt;
    }

    // Normalize cell densities per pixel
    float total_edge_density = 0.0f;
    float row_edge[4] = {0.0f};
    float col_edge[4] = {0.0f};
    for (int cy = 0; cy < 4; ++cy) {
        for (int cx = 0; cx < 4; ++cx) {
            int c = cy * 4 + cx;
            float inv_n = 1.0f / static_cast<float>(std::max(1, cell_edge_cnt[c]));
            cell_hv[c] *= inv_n;
            cell_diag[c] *= inv_n;
            cell_c2[c] *= inv_n;
            cell_s2[c] *= inv_n;
            cell_edge[c] *= inv_n;
            total_edge_density += cell_edge[c];
            row_edge[cy] += cell_edge[c];
            col_edge[cx] += cell_edge[c];
        }
    }

    float mean_edge = std::max(total_edge_density * (1.0f / 16.0f), 1e-4f);
    float inv_edge = 0.22f / mean_edge;

    // Layout harmonics across the 4x4 grid
    float tot_e = std::max(total_edge_density, 1e-4f);
    float vert_band_harmonic = ((row_edge[1] + row_edge[2]) - (row_edge[0] + row_edge[3])) / tot_e;
    float horiz_band_harmonic = ((col_edge[1] + col_edge[2]) - (col_edge[0] + col_edge[3])) / tot_e;
    float lr_asymmetry = ((col_edge[2] + col_edge[3]) - (col_edge[0] + col_edge[1])) / tot_e;

    float log_aspect = std::log(static_cast<float>(width) / static_cast<float>(height));
    float signed_aspect = std::tanh(0.85f * log_aspect);
    float abs_aspect = std::tanh(0.65f * std::abs(log_aspect)) - 0.35f;

    // Populate the 4 quadrants (quad = 0..3, base_idx = quad * 16)
    // Each quadrant covers a 2x2 block of the 4x4 grid:
    //   qy = quad / 2 (0..1), qx = quad % 2 (0..1)
    for (int quad = 0; quad < 4; ++quad) {
        int qy = quad / 2;
        int qx = quad % 2;
        int base = quad * 16;

        int c00 = (qy * 2 + 0) * 4 + (qx * 2 + 0);
        int c01 = (qy * 2 + 0) * 4 + (qx * 2 + 1);
        int c10 = (qy * 2 + 1) * 4 + (qx * 2 + 0);
        int c11 = (qy * 2 + 1) * 4 + (qx * 2 + 1);
        int sub_cells[4] = {c00, c01, c10, c11};

        // Bins 0..7: Positive directional edge energies in the 4 sub-cells of this quadrant
        for (int s = 0; s < 4; ++s) {
            int sc = sub_cells[s];
            out_vec[base + s * 2 + 0] = cell_hv[sc] * inv_edge;
            out_vec[base + s * 2 + 1] = cell_diag[sc] * inv_edge;
        }

        // Bins 8..10: Signed orientation anisotropy (Vertical vs Horizontal, +45 vs -45)
        out_vec[base + 8]  = (cell_c2[c00] + cell_c2[c01]) * inv_edge * 1.2f;
        out_vec[base + 9]  = (cell_c2[c10] + cell_c2[c11]) * inv_edge * 1.2f;
        out_vec[base + 10] = (cell_s2[c00] + cell_s2[c01] + cell_s2[c10] + cell_s2[c11]) * inv_edge * 1.2f;

        // Bin 11: Zero-centered local edge density contrast (relative to image mean edge density)
        float quad_mean_edge = 0.25f * (cell_edge[c00] + cell_edge[c01] + cell_edge[c10] + cell_edge[c11]);
        out_vec[base + 11] = ((quad_mean_edge - mean_edge) / mean_edge) * 0.35f;

        // Bins 12..13: Zero-centered sub-cell luminance & color silhouette
        int inner_c = (qy == 0 ? 1 : 2) * 4 + (qx == 0 ? 1 : 2);
        int outer_c = (qy == 0 ? 0 : 3) * 4 + (qx == 0 ? 0 : 3);
        out_vec[base + 12] = (cell_lum[inner_c] - mean_l) * 1.8f + (cell_rb[inner_c] - mean_rb) * 2.2f;
        out_vec[base + 13] = (cell_lum[outer_c] - mean_l) * 1.8f + (cell_rb[outer_c] - mean_rb) * 2.2f;

        // Bin 14: Per-quadrant opponent color + tone
        float q_rb = 0.25f * (cell_rb[c00] + cell_rb[c01] + cell_rb[c10] + cell_rb[c11]);
        float q_gm = 0.25f * (cell_gm[c00] + cell_gm[c01] + cell_gm[c10] + cell_gm[c11]);
        float q_lum = 0.25f * (cell_lum[c00] + cell_lum[c01] + cell_lum[c10] + cell_lum[c11]);
        out_vec[base + 14] = (q_rb * 2.2f) + (q_gm * 2.4f) + (q_lum - 0.55f) * 0.65f;
    }

    // Bin 15 in each quadrant: Aspect ratio, tone contrast, and layout harmonics
    float clamped_log_aspect = std::clamp(log_aspect, -1.8f, 1.8f);
    out_vec[0 * 16 + 15] = clamped_log_aspect * 0.65f;
    out_vec[1 * 16 + 15] = abs_aspect * 0.60f + (std_l - 0.14f) * 1.1f;
    out_vec[2 * 16 + 15] = vert_band_harmonic * 0.55f + (mean_rb * 1.5f);
    out_vec[3 * 16 + 15] = (horiz_band_harmonic * 0.35f) + (lr_asymmetry * 0.35f) + (mean_gm * 2.2f) + (mean_sat - 0.08f) * 0.8f;

    // L2 Normalization
    float sum_sq = 0.0f;
    for (size_t i = 0; i < SiftFeatureData::GLOBAL_DIM; ++i) {
        sum_sq += out_vec[i] * out_vec[i];
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
    return std::clamp(dot, 0.0f, 1.0f);
}

float SiftFeatureEngine::compute_keypoint_similarity(const SiftFeatureData& a, const SiftFeatureData& b, float nndr_ratio) {
    if (!a.valid || !b.valid) return 0.0f;
    float fast_sim = compute_fast_similarity(a.global_vector, b.global_vector);
    if (a.keypoints.empty() && b.keypoints.empty()) {
        return fast_sim;
    }
    if (a.keypoints.empty() || b.keypoints.empty()) {
        return std::clamp(fast_sim * 0.30f, 0.0f, 1.0f);
    }

    const size_t na = std::min(a.keypoints.size(), SiftFeatureData::MAX_KEYPOINTS);
    const size_t nb = std::min(b.keypoints.size(), SiftFeatureData::MAX_KEYPOINTS);
    const float nndr_sq = nndr_ratio * nndr_ratio;
    constexpr int MAX_ACCEPT_DIST_SQ = 165000; // Maximum squared L2 distance for a valid descriptor match

    int dist_matrix[SiftFeatureData::MAX_KEYPOINTS][SiftFeatureData::MAX_KEYPOINTS];
    for (size_t i = 0; i < na; ++i) {
        const uint8_t* desc_a = a.keypoints[i].descriptor.data();
        for (size_t j = 0; j < nb; ++j) {
            const uint8_t* desc_b = b.keypoints[j].descriptor.data();
            int dist_sq = 0;
            #pragma unroll(8)
            for (int d = 0; d < 128; ++d) {
                int diff = static_cast<int>(desc_a[d]) - static_cast<int>(desc_b[d]);
                dist_sq += diff * diff;
            }
            dist_matrix[i][j] = dist_sq;
        }
    }

    // For each keypoint i in a and j in b, compute background reference distance
    // (3rd-smallest or median distance) so repeated/symmetrical structures (e.g.
    // twin turrets, symmetric hull sides, checkerboard corners) do not self-veto,
    // while indistinctive descriptors are still rejected.
    int ref_dist_a[SiftFeatureData::MAX_KEYPOINTS];
    int second_dist_a[SiftFeatureData::MAX_KEYPOINTS];
    int best_dist_a[SiftFeatureData::MAX_KEYPOINTS];

    for (size_t i = 0; i < na; ++i) {
        int row[SiftFeatureData::MAX_KEYPOINTS];
        for (size_t j = 0; j < nb; ++j) row[j] = dist_matrix[i][j];
        std::sort(row, row + nb);
        best_dist_a[i] = row[0];
        second_dist_a[i] = (nb > 1) ? row[1] : std::numeric_limits<int>::max();
        size_t ref_idx = std::min(nb - 1, std::max<size_t>(1, nb / 3));
        ref_dist_a[i] = row[ref_idx];
    }

    int best_dist_b[SiftFeatureData::MAX_KEYPOINTS];
    for (size_t j = 0; j < nb; ++j) {
        int bmin = std::numeric_limits<int>::max();
        for (size_t i = 0; i < na; ++i) {
            if (dist_matrix[i][j] < bmin) bmin = dist_matrix[i][j];
        }
        best_dist_b[j] = bmin;
    }

    struct CandidatePair {
        uint8_t i;
        uint8_t j;
        float score;
    };
    std::vector<CandidatePair> candidates;
    candidates.reserve(na * 2);

    for (size_t i = 0; i < na; ++i) {
        for (size_t j = 0; j < nb; ++j) {
            int d_sq = dist_matrix[i][j];
            if (d_sq > MAX_ACCEPT_DIST_SQ) continue;

            // Must be at or near the best descriptor match for i in a
            if (d_sq > best_dist_a[i] + 18000 && static_cast<float>(d_sq) > 1.25f * static_cast<float>(best_dist_a[i])) {
                continue;
            }

            // Pass either standard Lowe ratio test against 2nd-best OR distinctiveness
            // ratio test against the 1/3-quantile reference distance (for repeated motifs)
            bool pass_lowe = (d_sq == 0) ||
                (second_dist_a[i] > 0 &&
                 static_cast<float>(d_sq) < nndr_sq * static_cast<float>(second_dist_a[i]));
            bool pass_ref = (ref_dist_a[i] > 0 &&
                 static_cast<float>(d_sq) < nndr_sq * static_cast<float>(ref_dist_a[i]));

            if (!pass_lowe && !pass_ref) continue;

            // Descriptor similarity weight in (0, 1] (1.0 when d_sq == 0)
            float w_desc = std::exp(-static_cast<float>(d_sq) * (1.0f / 110000.0f));

            // Spatial proximity weight in [0.55, 1.0] (breaks ties for repeated structures)
            float dx = a.keypoints[i].x - b.keypoints[j].x;
            float dy = a.keypoints[i].y - b.keypoints[j].y;
            float w_pos = 0.55f + 0.45f * std::exp(-(dx * dx + dy * dy) * (1.0f / 0.10f));

            // Scale consistency weight in [0.75, 1.0]
            float sa = std::max(a.keypoints[i].scale, 1e-4f);
            float sb = std::max(b.keypoints[j].scale, 1e-4f);
            float log_s = std::log(sa / sb);
            float w_scale = 0.75f + 0.25f * std::exp(-(log_s * log_s) * (1.0f / 0.50f));

            // Mutual nearest-neighbor bonus
            bool is_mutual = (d_sq <= best_dist_a[i] + 2000) && (d_sq <= best_dist_b[j] + 2000);
            float w_mut = is_mutual ? 1.0f : (pass_lowe ? 0.88f : 0.78f);

            candidates.push_back({static_cast<uint8_t>(i), static_cast<uint8_t>(j), w_desc * w_pos * w_scale * w_mut});
        }
    }

    // Sort candidates by score descending for greedy 1-to-1 bipartite matching
    std::sort(candidates.begin(), candidates.end(), [](const CandidatePair& x, const CandidatePair& y) {
        return x.score > y.score;
    });

    bool used_a[SiftFeatureData::MAX_KEYPOINTS] = {false};
    bool used_b[SiftFeatureData::MAX_KEYPOINTS] = {false};
    std::vector<CandidatePair> matched;
    matched.reserve(std::min(na, nb));

    for (const auto& c : candidates) {
        if (!used_a[c.i] && !used_b[c.j]) {
            used_a[c.i] = true;
            used_b[c.j] = true;
            matched.push_back(c);
        }
    }

    // Geometric translation consensus (2D shift inlier check):
    // Find the dominant translation offset (dx, dy) = (x_a - x_b, y_a - y_b)
    // across matched pairs so spatially coherent matches (even when translated)
    // are boosted over scattered accidental matches.
    float weighted_matches = 0.0f;
    if (!matched.empty()) {
        float best_cluster_weight = 0.0f;
        size_t best_center = 0;
        for (size_t k = 0; k < matched.size(); ++k) {
            float tx_k = a.keypoints[matched[k].i].x - b.keypoints[matched[k].j].x;
            float ty_k = a.keypoints[matched[k].i].y - b.keypoints[matched[k].j].y;
            float cluster_w = 0.0f;
            for (size_t m = 0; m < matched.size(); ++m) {
                float tx_m = a.keypoints[matched[m].i].x - b.keypoints[matched[m].j].x;
                float ty_m = a.keypoints[matched[m].i].y - b.keypoints[matched[m].j].y;
                float dtx = tx_m - tx_k;
                float dty = ty_m - ty_k;
                if (dtx * dtx + dty * dty <= 0.0225f) { // within radius 0.15
                    cluster_w += matched[m].score;
                }
            }
            if (cluster_w > best_cluster_weight) {
                best_cluster_weight = cluster_w;
                best_center = k;
            }
        }

        float tx_c = a.keypoints[matched[best_center].i].x - b.keypoints[matched[best_center].j].x;
        float ty_c = a.keypoints[matched[best_center].i].y - b.keypoints[matched[best_center].j].y;
        for (const auto& m : matched) {
            float tx_m = a.keypoints[m.i].x - b.keypoints[m.j].x;
            float ty_m = a.keypoints[m.i].y - b.keypoints[m.j].y;
            float dtx = tx_m - tx_c;
            float dty = ty_m - ty_c;
            bool inlier = (dtx * dtx + dty * dty <= 0.0225f);
            weighted_matches += m.score * (inlier ? 1.0f : 0.55f);
        }
    }

    // Use max(na, nb) as denominator so images with very few keypoints cannot
    // achieve 100% similarity against detailed 48-keypoint queries, while
    // self-similarity (na == nb) remains 1.0.
    size_t denom_kpts = std::max(na, nb);
    float raw_ratio = (denom_kpts > 0) ? (weighted_matches / static_cast<float>(denom_kpts)) : 0.0f;
    raw_ratio = std::clamp(raw_ratio, 0.0f, 1.0f);
    // Concave response curve: maps 0 -> 0, 0.25 -> 0.44, 1.0 -> 1.0
    float match_ratio = std::sqrt(raw_ratio) * 0.55f + raw_ratio * 0.45f;

    // Blend keypoint match ratio with fast global similarity
    float final_score = std::clamp(match_ratio * 0.65f + fast_sim * 0.35f, 0.0f, 1.0f);
    return final_score;
}

float SiftFeatureEngine::compute_similarity(const SiftFeatureData& a, const SiftFeatureData& b, bool high_accuracy) {
    if (high_accuracy) {
        return compute_keypoint_similarity(a, b);
    }
    return compute_fast_similarity(a.global_vector, b.global_vector);
}
