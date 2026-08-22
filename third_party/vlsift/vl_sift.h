/** @file vl_sift.h
 ** @brief SIFT Feature Detector and Descriptor (VLFeat C++17 Port)
 ** @author Andrea Vedaldi, Brian Fulkerson
 **/

/*
Copyright (C) 2007-12 Andrea Vedaldi and Brian Fulkerson.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace vl {

struct SiftKeypoint {
    int o = 0;           /**< Octave index */
    int ix = 0;          /**< Integer x */
    int iy = 0;          /**< Integer y */
    int is = 0;          /**< Integer scale level */
    float x = 0.0f;      /**< Subpixel x */
    float y = 0.0f;      /**< Subpixel y */
    float s = 0.0f;      /**< Subpixel s */
    float sigma = 0.0f;  /**< Effective scale sigma */
};

class SiftFilter {
public:
    SiftFilter(int width, int height, int noctaves = -1, int nlevels = 3, int o_min = 0);
    ~SiftFilter() = default;

    // Disallow copy, allow move
    SiftFilter(const SiftFilter&) = delete;
    SiftFilter& operator=(const SiftFilter&) = delete;
    SiftFilter(SiftFilter&&) = default;
    SiftFilter& operator=(SiftFilter&&) = default;

    // Process octaves
    int process_first_octave(const float* im);
    int process_next_octave();

    // Detect keypoints in current octave
    void detect();

    // Keypoint orientations and descriptors
    int calc_keypoint_orientations(double angles[4], const SiftKeypoint* k);
    void calc_keypoint_descriptor(float* descr, const SiftKeypoint* k, double angle0);

    // Getters / Setters
    const std::vector<SiftKeypoint>& keypoints() const { return keys_; }
    int num_keypoints() const { return static_cast<int>(keys_.size()); }

    int octave_width() const { return octave_width_; }
    int octave_height() const { return octave_height_; }
    int current_octave() const { return o_cur_; }

    void set_peak_thresh(double t) { peak_thresh_ = t; }
    void set_edge_thresh(double t) { edge_thresh_ = t; }
    void set_norm_thresh(double t) { norm_thresh_ = t; }
    void set_magnif(double m) { magnif_ = m; }

    double peak_thresh() const { return peak_thresh_; }
    double edge_thresh() const { return edge_thresh_; }
    double norm_thresh() const { return norm_thresh_; }
    double magnif() const { return magnif_; }

private:
    float* get_octave(int s) {
        return octave_.data() + (octave_width_ * octave_height_) * (s - s_min_);
    }
    const float* get_octave(int s) const {
        return octave_.data() + (octave_width_ * octave_height_) * (s - s_min_);
    }

    void smooth(float* output, float* temp, const float* input, int width, int height, double sigma);
    void update_gradient();

    int width_ = 0;
    int height_ = 0;
    int O_ = 0;
    int S_ = 3;
    int o_min_ = 0;
    int s_min_ = -1;
    int s_max_ = 4;
    int o_cur_ = 0;

    double sigman_ = 0.5;
    double sigma0_ = 1.6;
    double sigmak_ = 1.0;
    double dsigma0_ = 0.0;

    int octave_width_ = 0;
    int octave_height_ = 0;

    std::vector<float> temp_;
    std::vector<float> octave_;
    std::vector<float> dog_;
    std::vector<float> grad_;
    int grad_o_ = -100;

    std::vector<float> gauss_filter_;
    double gauss_filter_sigma_ = 0.0;
    int gauss_filter_width_ = 0;

    std::vector<SiftKeypoint> keys_;

    double peak_thresh_ = 0.0;
    double edge_thresh_ = 10.0;
    double norm_thresh_ = 0.0;
    double magnif_ = 3.0;
    double window_size_ = 2.0; // NBP / 2
};

} // namespace vl
