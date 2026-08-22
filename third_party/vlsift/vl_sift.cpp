/** @file vl_sift.cpp
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

#include "vl_sift.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <limits>

namespace vl {

namespace {
    constexpr double VL_PI = 3.14159265358979323846;
    constexpr double VL_EPSILON_D = 1e-11;
    constexpr float  VL_EPSILON_F = 1e-7f;

    constexpr int NBO = 8; // Number of orientation bins
    constexpr int NBP = 4; // Number of spatial bins

    constexpr int EXPN_SZ = 256;
    constexpr double EXPN_MAX = 25.0;

    struct ExpnTable {
        float tab[EXPN_SZ + 1];
        ExpnTable() {
            for (int k = 0; k <= EXPN_SZ; ++k) {
                tab[k] = static_cast<float>(std::exp(-static_cast<double>(k) * (EXPN_MAX / EXPN_SZ)));
            }
        }
        inline float get(float x) const {
            if (x >= static_cast<float>(EXPN_MAX)) return 0.0f;
            if (x <= 0.0f) return 1.0f;
            float val = x * (EXPN_SZ / static_cast<float>(EXPN_MAX));
            int i = static_cast<int>(val);
            float r = val - static_cast<float>(i);
            return tab[i] + r * (tab[i + 1] - tab[i]);
        }
    };
    static const ExpnTable g_expn;

    inline float fast_expn(float x) {
        return g_expn.get(x);
    }

    inline float fast_atan2(float y, float x) {
        return std::atan2(y, x);
    }

    inline float fast_sqrt(float x) {
        return std::sqrt(x);
    }

    inline float mod_2pi(float x) {
        while (x < 0.0f) x += static_cast<float>(2.0 * VL_PI);
        while (x >= static_cast<float>(2.0 * VL_PI)) x -= static_cast<float>(2.0 * VL_PI);
        return x;
    }

    void copy_and_upsample_rows(float* dst, const float* src, int width, int height) {
        for (int y = 0; y < height; ++y) {
            float a = *src++;
            float b = a;
            for (int x = 0; x < width - 1; ++x) {
                b = *src++;
                *dst = a; dst += height;
                *dst = 0.5f * (a + b); dst += height;
                a = b;
            }
            *dst = b; dst += height;
            *dst = b; dst += height;
            dst += 1 - width * 2 * height;
        }
    }

    void copy_and_downsample(float* dst, const float* src, int width, int height, int d) {
        int step = 1 << d;
        for (int y = 0; y < height; y += step) {
            const float* srcrowp = src + y * width;
            for (int x = 0; x < width - (step - 1); x += step) {
                *dst++ = *srcrowp;
                srcrowp += step;
            }
        }
    }

    void imconvcol_vf(float* dst, size_t dst_stride,
                     const float* src, size_t src_width, size_t src_height, size_t src_stride,
                     const float* filt, int filt_begin, int filt_end,
                     int step, bool transpose)
    {
        for (size_t x = 0; x < src_width; ++x) {
            for (size_t y = 0; y < src_height; y += step) {
                float acc = 0.0f;
                for (int f = filt_begin; f <= filt_end; ++f) {
                    int p = static_cast<int>(y) - f;
                    float val;
                    if (p < 0) {
                        val = src[x]; // continuity at top
                    } else if (p >= static_cast<int>(src_height)) {
                        val = src[x + (src_height - 1) * src_stride]; // continuity at bottom
                    } else {
                        val = src[x + p * src_stride];
                    }
                    acc += val * filt[f - filt_begin];
                }
                if (transpose) {
                    dst[(y / step) + x * dst_stride] = acc;
                } else {
                    dst[x + (y / step) * dst_stride] = acc;
                }
            }
        }
    }

    inline float normalize_histogram(float* begin, float* end) {
        float norm = 0.0f;
        for (float* iter = begin; iter != end; ++iter) {
            norm += (*iter) * (*iter);
        }
        norm = fast_sqrt(norm) + VL_EPSILON_F;
        for (float* iter = begin; iter != end; ++iter) {
            *iter /= norm;
        }
        return norm;
    }
}

SiftFilter::SiftFilter(int width, int height, int noctaves, int nlevels, int o_min)
    : width_(width), height_(height), S_(nlevels), o_min_(o_min), s_min_(-1), s_max_(nlevels + 1), o_cur_(o_min)
{
    int w = (o_min < 0) ? (width << (-o_min)) : (width >> o_min);
    int h = (o_min < 0) ? (height << (-o_min)) : (height >> o_min);
    size_t nel = static_cast<size_t>(w) * h;

    if (noctaves < 0) {
        int min_dim = std::min(width, height);
        noctaves = std::max(static_cast<int>(std::floor(std::log2(min_dim))) - o_min - 3, 1);
    }
    O_ = noctaves;

    temp_.resize(nel);
    octave_.resize(nel * (s_max_ - s_min_ + 1));
    dog_.resize(nel * (s_max_ - s_min_));
    grad_.resize(nel * 2 * (s_max_ - s_min_));

    sigman_ = 0.5;
    sigmak_ = std::pow(2.0, 1.0 / nlevels);
    sigma0_ = 1.6 * sigmak_;
    dsigma0_ = sigma0_ * std::sqrt(1.0 - 1.0 / (sigmak_ * sigmak_));
    window_size_ = NBP / 2.0;
}

void SiftFilter::smooth(float* output, float* temp, const float* input, int width, int height, double sigma) {
    if (gauss_filter_sigma_ != sigma) {
        gauss_filter_width_ = std::max(static_cast<int>(std::ceil(4.0 * sigma)), 1);
        gauss_filter_sigma_ = sigma;
        gauss_filter_.resize(2 * gauss_filter_width_ + 1);

        float acc = 0.0f;
        for (int j = 0; j < 2 * gauss_filter_width_ + 1; ++j) {
            float d = (static_cast<float>(j - gauss_filter_width_)) / static_cast<float>(sigma);
            gauss_filter_[j] = std::exp(-0.5f * (d * d));
            acc += gauss_filter_[j];
        }
        for (int j = 0; j < 2 * gauss_filter_width_ + 1; ++j) {
            gauss_filter_[j] /= acc;
        }
    }

    if (gauss_filter_width_ == 0) {
        std::memcpy(output, input, sizeof(float) * width * height);
        return;
    }

    imconvcol_vf(temp, height, input, width, height, width,
                 gauss_filter_.data(), -gauss_filter_width_, gauss_filter_width_, 1, true);

    imconvcol_vf(output, width, temp, height, width, height,
                 gauss_filter_.data(), -gauss_filter_width_, gauss_filter_width_, 1, true);
}

int SiftFilter::process_first_octave(const float* im) {
    o_cur_ = o_min_;
    keys_.clear();

    octave_width_  = (o_cur_ < 0) ? (width_ << (-o_cur_)) : (width_ >> o_cur_);
    octave_height_ = (o_cur_ < 0) ? (height_ << (-o_cur_)) : (height_ >> o_cur_);
    int w = octave_width_;
    int h = octave_height_;

    if (O_ == 0) return -1; // EOF

    float* oct = get_octave(s_min_);

    if (o_min_ < 0) {
        copy_and_upsample_rows(temp_.data(), im, width_, height_);
        copy_and_upsample_rows(oct, temp_.data(), height_, 2 * width_);
        for (int o = -1; o > o_min_; --o) {
            copy_and_upsample_rows(temp_.data(), oct, width_ << -o, height_ << -o);
            copy_and_upsample_rows(oct, temp_.data(), width_ << -o, 2 * (height_ << -o));
        }
    } else if (o_min_ > 0) {
        copy_and_downsample(oct, im, width_, height_, o_min_);
    } else {
        std::memcpy(oct, im, sizeof(float) * width_ * height_);
    }

    double sa = sigma0_ * std::pow(sigmak_, s_min_);
    double sb = sigman_ * std::pow(2.0, -o_min_);

    if (sa > sb) {
        double sd = std::sqrt(sa * sa - sb * sb);
        smooth(oct, temp_.data(), oct, w, h, sd);
    }

    for (int s = s_min_ + 1; s <= s_max_; ++s) {
        double sd = dsigma0_ * std::pow(sigmak_, s);
        smooth(get_octave(s), temp_.data(), get_octave(s - 1), w, h, sd);
    }

    return 0;
}

int SiftFilter::process_next_octave() {
    if (o_cur_ == o_min_ + O_ - 1) return -1; // EOF

    int s_best = std::min(s_min_ + S_, s_max_);
    int w = octave_width_;
    int h = octave_height_;
    const float* pt = get_octave(s_best);
    float* oct = octave_.data();

    copy_and_downsample(oct, pt, w, h, 1);

    o_cur_ += 1;
    keys_.clear();

    octave_width_  = (o_cur_ < 0) ? (width_ << (-o_cur_)) : (width_ >> o_cur_);
    octave_height_ = (o_cur_ < 0) ? (height_ << (-o_cur_)) : (height_ >> o_cur_);
    w = octave_width_;
    h = octave_height_;

    double sa = sigma0_ * std::pow(sigmak_, s_min_);
    double sb = sigma0_ * std::pow(sigmak_, s_best - S_);

    if (sa > sb) {
        double sd = std::sqrt(sa * sa - sb * sb);
        smooth(oct, temp_.data(), oct, w, h, sd);
    }

    for (int s = s_min_ + 1; s <= s_max_; ++s) {
        double sd = dsigma0_ * std::pow(sigmak_, s);
        smooth(get_octave(s), temp_.data(), get_octave(s - 1), w, h, sd);
    }

    return 0;
}

void SiftFilter::detect() {
    int w = octave_width_;
    int h = octave_height_;
    double te = edge_thresh_;
    double tp = peak_thresh_;

    int const xo = 1;
    int const yo = w;
    int const so = w * h;

    double xper = std::pow(2.0, o_cur_);

    keys_.clear();

    // Compute Difference of Gaussians (DoG)
    float* pt = dog_.data();
    for (int s = s_min_; s <= s_max_ - 1; ++s) {
        const float* src_a = get_octave(s);
        const float* src_b = get_octave(s + 1);
        const float* end_a = src_a + w * h;
        while (src_a != end_a) {
            *pt++ = *src_b++ - *src_a++;
        }
    }

    // Find local maxima / minima in DoG
    std::vector<SiftKeypoint> raw_keys;

    pt = dog_.data() + xo + yo + so;
    for (int s = s_min_ + 1; s <= s_max_ - 2; ++s) {
        for (int y = 1; y < h - 1; ++y) {
            for (int x = 1; x < w - 1; ++x) {
                float v = *pt;

#define CHECK_NEIGHBORS(CMP, SGN) \
                (v CMP##= SGN 0.8f * tp && \
                 v CMP *(pt + xo) && v CMP *(pt - xo) && \
                 v CMP *(pt + so) && v CMP *(pt - so) && \
                 v CMP *(pt + yo) && v CMP *(pt - yo) && \
                 v CMP *(pt + yo + xo) && v CMP *(pt + yo - xo) && \
                 v CMP *(pt - yo + xo) && v CMP *(pt - yo - xo) && \
                 v CMP *(pt + xo + so) && v CMP *(pt - xo + so) && \
                 v CMP *(pt + yo + so) && v CMP *(pt - yo + so) && \
                 v CMP *(pt + yo + xo + so) && v CMP *(pt + yo - xo + so) && \
                 v CMP *(pt - yo + xo + so) && v CMP *(pt - yo - xo + so) && \
                 v CMP *(pt + xo - so) && v CMP *(pt - xo - so) && \
                 v CMP *(pt + yo - so) && v CMP *(pt - yo - so) && \
                 v CMP *(pt + yo + xo - so) && v CMP *(pt + yo - xo - so) && \
                 v CMP *(pt - yo + xo - so) && v CMP *(pt - yo - xo - so))

                if (CHECK_NEIGHBORS(>, +) || CHECK_NEIGHBORS(<, -)) {
                    SiftKeypoint k;
                    k.ix = x; k.iy = y; k.is = s;
                    raw_keys.push_back(k);
                }
                pt += 1;
            }
            pt += 2;
        }
        pt += 2 * yo;
    }

    // Refine keypoints (3D quadratic interpolation & threshold checks)
    for (const auto& raw_k : raw_keys) {
        int x = raw_k.ix;
        int y = raw_k.iy;
        int s = raw_k.is;

        double Dx = 0, Dy = 0, Ds = 0;
        double Dxx = 0, Dyy = 0, Dss = 0, Dxy = 0, Dxs = 0, Dys = 0;
        double A[9], b[3];
        int dx = 0, dy = 0;
        const float* p_dog = dog_.data() + xo * x + yo * y + so * (s - s_min_);

        for (int iter = 0; iter < 5; ++iter) {
            x += dx;
            y += dy;

            p_dog = dog_.data() + xo * x + yo * y + so * (s - s_min_);
#define at(dx, dy, ds) (*(p_dog + (dx)*xo + (dy)*yo + (ds)*so))
#define Aat(i, j) (A[(i) + (j)*3])

            Dx = 0.5 * (at(+1, 0, 0) - at(-1, 0, 0));
            Dy = 0.5 * (at(0, +1, 0) - at(0, -1, 0));
            Ds = 0.5 * (at(0, 0, +1) - at(0, 0, -1));

            Dxx = (at(+1, 0, 0) + at(-1, 0, 0) - 2.0 * at(0, 0, 0));
            Dyy = (at(0, +1, 0) + at(0, -1, 0) - 2.0 * at(0, 0, 0));
            Dss = (at(0, 0, +1) + at(0, 0, -1) - 2.0 * at(0, 0, 0));

            Dxy = 0.25 * (at(+1, +1, 0) + at(-1, -1, 0) - at(-1, +1, 0) - at(+1, -1, 0));
            Dxs = 0.25 * (at(+1, 0, +1) + at(-1, 0, -1) - at(-1, 0, +1) - at(+1, 0, -1));
            Dys = 0.25 * (at(0, +1, +1) + at(0, -1, -1) - at(0, -1, +1) - at(0, +1, -1));

            Aat(0, 0) = Dxx; Aat(1, 1) = Dyy; Aat(2, 2) = Dss;
            Aat(0, 1) = Aat(1, 0) = Dxy;
            Aat(0, 2) = Aat(2, 0) = Dxs;
            Aat(1, 2) = Aat(2, 1) = Dys;

            b[0] = -Dx; b[1] = -Dy; b[2] = -Ds;

            // Solve 3x3 linear system via Gaussian elimination with partial pivoting
            for (int j = 0; j < 3; ++j) {
                double maxabsa = 0;
                int maxi = -1;
                for (int i = j; i < 3; ++i) {
                    double absa = std::abs(Aat(i, j));
                    if (absa > maxabsa) {
                        maxabsa = absa;
                        maxi = i;
                    }
                }
                if (maxabsa < 1e-10) {
                    b[0] = b[1] = b[2] = 0;
                    break;
                }
                int i = maxi;
                for (int jj = j; jj < 3; ++jj) {
                    double tmp = Aat(i, jj); Aat(i, jj) = Aat(j, jj); Aat(j, jj) = tmp;
                    Aat(j, jj) /= Aat(j, j); // Note: Aat(j,j) is maxa
                }
                double tmp = b[j]; b[j] = b[i]; b[i] = tmp;
                b[j] /= Aat(j, j);

                for (int ii = j + 1; ii < 3; ++ii) {
                    double mult = Aat(ii, j);
                    for (int jj = j; jj < 3; ++jj) {
                        Aat(ii, jj) -= mult * Aat(j, jj);
                    }
                    b[ii] -= mult * b[j];
                }
            }

            for (int i = 2; i > 0; --i) {
                double val = b[i];
                for (int ii = i - 1; ii >= 0; --ii) {
                    b[ii] -= val * Aat(ii, i);
                }
            }

            dx = ((b[0] > 0.6 && x < w - 2) ? 1 : 0) + ((b[0] < -0.6 && x > 1) ? -1 : 0);
            dy = ((b[1] > 0.6 && y < h - 2) ? 1 : 0) + ((b[1] < -0.6 && y > 1) ? -1 : 0);
            if (dx == 0 && dy == 0) break;
        }

        double val = at(0, 0, 0) + 0.5 * (Dx * b[0] + Dy * b[1] + Ds * b[2]);
        double det = (Dxx * Dyy - Dxy * Dxy);
        double tr = (Dxx + Dyy);
        double score = (det > 0) ? ((tr * tr) / det) : -1.0;

        double xn = x + b[0];
        double yn = y + b[1];
        double sn = s + b[2];

        bool good = (std::abs(val) > tp) &&
                    (score >= 0 && score < (te + 1.0) * (te + 1.0) / te) &&
                    (std::abs(b[0]) < 1.5 && std::abs(b[1]) < 1.5 && std::abs(b[2]) < 1.5) &&
                    (xn >= 0 && xn <= w - 1 && yn >= 0 && yn <= h - 1) &&
                    (sn >= s_min_ && sn <= s_max_);

        if (good) {
            SiftKeypoint k;
            k.o = o_cur_;
            k.ix = x; k.iy = y; k.is = s;
            k.s = static_cast<float>(sn);
            k.x = static_cast<float>(xn * xper);
            k.y = static_cast<float>(yn * xper);
            k.sigma = static_cast<float>(sigma0_ * std::pow(2.0, sn / S_) * xper);
            keys_.push_back(k);
        }
    }
}

void SiftFilter::update_gradient() {
    int w = octave_width_;
    int h = octave_height_;
    int const xo = 1;
    int const yo = w;
    int const so = h * w;

    if (grad_o_ == o_cur_) return;

    for (int s = s_min_ + 1; s <= s_max_ - 2; ++s) {
        const float* src = get_octave(s);
        float* grad = grad_.data() + 2 * so * (s - s_min_ - 1);

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float gx = 0.0f, gy = 0.0f;
                if (x == 0) gx = src[1] - src[0];
                else if (x == w - 1) gx = src[w - 1] - src[w - 2];
                else gx = 0.5f * (src[x + 1] - src[x - 1]);

                if (y == 0) gy = src[yo] - src[0];
                else if (y == h - 1) gy = src[0] - src[-yo];
                else gy = 0.5f * (src[yo] - src[-yo]);

                *grad++ = fast_sqrt(gx * gx + gy * gy);
                *grad++ = mod_2pi(fast_atan2(gy, gx) + static_cast<float>(2.0 * VL_PI));
                ++src;
            }
        }
    }
    grad_o_ = o_cur_;
}

int SiftFilter::calc_keypoint_orientations(double angles[4], const SiftKeypoint* k) {
    double const winf = 1.5;
    double xper = std::pow(2.0, o_cur_);

    int w = octave_width_;
    int h = octave_height_;
    int const xo = 2;
    int const yo = 2 * w;
    int const so = 2 * w * h;

    double x = k->x / xper;
    double y = k->y / xper;
    double sigma = k->sigma / xper;

    int xi = static_cast<int>(x + 0.5);
    int yi = static_cast<int>(y + 0.5);
    int si = k->is;

    double sigmaw = winf * sigma;
    int W = std::max(static_cast<int>(std::floor(3.0 * sigmaw)), 1);

    if (k->o != o_cur_) return 0;
    if (xi < 0 || xi > w - 1 || yi < 0 || yi > h - 1 || si < s_min_ + 1 || si > s_max_ - 2) return 0;

    update_gradient();

    constexpr int nbins = 36;
    double hist[nbins] = {0.0};

    const float* pt = grad_.data() + xo * xi + yo * yi + so * (si - s_min_ - 1);

    for (int ys = std::max(-W, -yi); ys <= std::min(W, h - 1 - yi); ++ys) {
        for (int xs = std::max(-W, -xi); xs <= std::min(W, w - 1 - xi); ++xs) {
            double dx = static_cast<double>(xi + xs) - x;
            double dy = static_cast<double>(yi + ys) - y;
            double r2 = dx * dx + dy * dy;
            if (r2 >= W * W + 0.6) continue;

            float wgt = fast_expn(static_cast<float>(r2 / (2.0 * sigmaw * sigmaw)));
            float mod = *(pt + xs * xo + ys * yo);
            float ang = *(pt + xs * xo + ys * yo + 1);

            int bin = static_cast<int>(std::floor(nbins * ang / (2.0 * VL_PI))) % nbins;
            hist[bin] += mod * wgt;
        }
    }

    // Smooth histogram 6 times
    for (int iter = 0; iter < 6; ++iter) {
        double prev = hist[nbins - 1];
        double first = hist[0];
        for (int i = 0; i < nbins - 1; ++i) {
            double newh = (prev + hist[i] + hist[(i + 1) % nbins]) / 3.0;
            prev = hist[i];
            hist[i] = newh;
        }
        hist[nbins - 1] = (prev + hist[nbins - 1] + first) / 3.0;
    }

    double maxh = 0.0;
    for (int i = 0; i < nbins; ++i) maxh = std::max(maxh, hist[i]);

    int nangles = 0;
    for (int i = 0; i < nbins; ++i) {
        double h0 = hist[i];
        double hm = hist[(i - 1 + nbins) % nbins];
        double hp = hist[(i + 1) % nbins];

        if (h0 > 0.8 * maxh && h0 > hm && h0 > hp) {
            double di = -0.5 * (hp - hm) / (hp + hm - 2.0 * h0);
            double th = 2.0 * VL_PI * (i + di + 0.5) / nbins;
            angles[nangles++] = th;
            if (nangles == 4) break;
        }
    }
    return nangles;
}

void SiftFilter::calc_keypoint_descriptor(float* descr, const SiftKeypoint* k, double angle0) {
    double const magnif = magnif_;
    double xper = std::pow(2.0, o_cur_);

    int w = octave_width_;
    int h = octave_height_;
    int const xo = 2;
    int const yo = 2 * w;
    int const so = 2 * w * h;

    int xi = static_cast<int>(k->x / xper + 0.5);
    int yi = static_cast<int>(k->y / xper + 0.5);
    int si = k->is;

    double st0 = std::sin(angle0);
    double ct0 = std::cos(angle0);
    double SBP = magnif * (k->sigma / xper) + VL_EPSILON_D;
    int W = static_cast<int>(std::floor(std::sqrt(2.0) * SBP * (NBP + 1) / 2.0 + 0.5));

    int const binto = 1;
    int const binyo = NBO * NBP;
    int const binxo = NBO;

    if (xi < 0 || xi >= w || yi < 0 || yi >= h - 1) return;

    std::memset(descr, 0, sizeof(float) * NBO * NBP * NBP);

    const float* pt = grad_.data() + xi * xo + yi * yo + so * (si - s_min_ - 1);
    float* dpt = descr + (NBP / 2) * binyo + (NBP / 2) * binxo;

#define atd(dbinx, dbiny, dbint) *(dpt + (dbint)*binto + (dbiny)*binyo + (dbinx)*binxo)

    for (int dyi = std::max(-W, -yi); dyi <= std::min(W, h - yi - 1); ++dyi) {
        for (int dxi = std::max(-W, -xi); dxi <= std::min(W, w - xi - 1); ++dxi) {
            float mod = *(pt + dxi * xo + dyi * yo + 0);
            float angle = *(pt + dxi * xo + dyi * yo + 1);
            float theta = mod_2pi(angle - static_cast<float>(angle0));

            float dx = static_cast<float>(xi + dxi) - (k->x / static_cast<float>(xper));
            float dy = static_cast<float>(yi + dyi) - (k->y / static_cast<float>(xper));

            float nx = static_cast<float>((ct0 * dx + st0 * dy) / SBP);
            float ny = static_cast<float>((-st0 * dx + ct0 * dy) / SBP);
            float nt = static_cast<float>(NBO * theta / (2.0 * VL_PI));

            float wsigma = static_cast<float>(window_size_);
            float win = fast_expn((nx * nx + ny * ny) / (2.0f * wsigma * wsigma));

            int binx = static_cast<int>(std::floor(nx - 0.5f));
            int biny = static_cast<int>(std::floor(ny - 0.5f));
            int bint = static_cast<int>(std::floor(nt));
            float rbinx = nx - (binx + 0.5f);
            float rbiny = ny - (biny + 0.5f);
            float rbint = nt - bint;

            for (int dbinx = 0; dbinx < 2; ++dbinx) {
                for (int dbiny = 0; dbiny < 2; ++dbiny) {
                    for (int dbint = 0; dbint < 2; ++dbint) {
                        if (binx + dbinx >= -(NBP / 2) && binx + dbinx < (NBP / 2) &&
                            biny + dbiny >= -(NBP / 2) && biny + dbiny < (NBP / 2))
                        {
                            float weight = win * mod
                                         * std::abs(1.0f - dbinx - rbinx)
                                         * std::abs(1.0f - dbiny - rbiny)
                                         * std::abs(1.0f - dbint - rbint);
                            atd(binx + dbinx, biny + dbiny, (bint + dbint) % NBO) += weight;
                        }
                    }
                }
            }
        }
    }

    float norm = normalize_histogram(descr, descr + NBO * NBP * NBP);
    int numSamples = (std::min(W, w - xi - 1) - std::max(-W, -xi) + 1) *
                     (std::min(W, h - yi - 1) - std::max(-W, -yi) + 1);

    if (norm_thresh_ && norm < norm_thresh_ * numSamples) {
        std::memset(descr, 0, sizeof(float) * NBO * NBP * NBP);
    } else {
        for (int bin = 0; bin < NBO * NBP * NBP; ++bin) {
            if (descr[bin] > 0.2f) descr[bin] = 0.2f;
        }
        normalize_histogram(descr, descr + NBO * NBP * NBP);
    }
}

} // namespace vl
