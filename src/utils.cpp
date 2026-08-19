/*
 * utils.cpp - Utility functions for picscan
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

#include "utils.h"
#include <thread>
#include <mutex>
#include <algorithm>
#include <filesystem>

/* Utils is where we put the implementation
 * sections for stb */

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// Timer implementation
void Timer::start(const std::string& phase) {
    active_timers_[phase] = std::chrono::high_resolution_clock::now();
}

void Timer::stop(const std::string& phase) {
    auto it = active_timers_.find(phase);
    if (it != active_timers_.end()) {
	auto end_time = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - it->second);
	phase_times_[phase] += duration;
	active_timers_.erase(it);
    }
}

void Timer::print_summary() const {
    if (phase_times_.empty()) {
	std::cout << "No timing data recorded." << std::endl;
	return;
    }

    std::cout << "\n=== Timing Summary ===" << std::endl;
    std::cout << std::left << std::setw(25) << "Phase" << "Time (seconds)" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    auto total_time = std::chrono::milliseconds(0);
    for (const auto& entry : phase_times_) {
	total_time += entry.second;
    }

    for (const auto& entry : phase_times_) {
	double seconds = entry.second.count() / 1000.0;
	double percentage = (total_time.count() > 0) ? (entry.second.count() * 100.0 / total_time.count()) : 0.0;
	std::cout << std::left << std::setw(25) << entry.first
	    << std::fixed << std::setprecision(3) << seconds
	    << " (" << std::setprecision(1) << percentage << "%)" << std::endl;
    }

    std::cout << std::string(40, '-') << std::endl;
    std::cout << std::left << std::setw(25) << "TOTAL"
	<< std::fixed << std::setprecision(3) << (total_time.count() / 1000.0) << std::endl;
}

// StatusReporter implementation
StatusReporter::StatusReporter(int interval_seconds)
    : interval_seconds_(interval_seconds), current_status_("Initializing..."),
    total_count_(0), current_count_(0), running_(false), reporter_thread_(nullptr) {
    }

StatusReporter::~StatusReporter() {
    stop();
}

void StatusReporter::update_status(const std::string& message) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_status_ = message;
}

void StatusReporter::set_total_count(int total) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    total_count_ = total;
}

void StatusReporter::set_current_count(int current) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    current_count_ = current;
}

void StatusReporter::start() {
    if (running_) return;

    running_ = true;
    reporter_thread_ = new std::thread(&StatusReporter::report_thread, this);
}

void StatusReporter::stop() {
    if (!running_) return;

    running_ = false;
    if (reporter_thread_) {
	reporter_thread_->join();
	delete reporter_thread_;
	reporter_thread_ = nullptr;
    }
}

void StatusReporter::report_thread() {
    while (running_) {
	std::this_thread::sleep_for(std::chrono::seconds(interval_seconds_));

	if (!running_) break;

	std::lock_guard<std::mutex> lock(status_mutex_);
	std::cout << "[STATUS] " << current_status_;
	if (total_count_ > 0) {
	    std::cout << " (" << current_count_ << "/" << total_count_ << " - "
		<< std::fixed << std::setprecision(1)
		<< (current_count_ * 100.0 / total_count_) << "%)";
	}
	std::cout << std::endl;
    }
}

// Utility functions
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality) {
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

bool is_image_file(const std::string& filepath) {
    auto ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" ||
            ext == ".tga" || ext == ".webp" || ext == ".tif" || ext == ".tiff");
}

#include <webp/decode.h>
#include <tiffio.h>
#include <fstream>

bool get_image_info(const std::string& filepath, int* width, int* height) {
    if (!width || !height) return false;
    *width = 0;
    *height = 0;

    auto ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".webp") {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) return false;
        char header[64];
        file.read(header, sizeof(header));
        size_t bytes_read = file.gcount();
        if (bytes_read < 30) return false;
        return (WebPGetInfo(reinterpret_cast<const uint8_t*>(header), bytes_read, width, height) != 0);
    } else if (ext == ".tif" || ext == ".tiff") {
        TIFFSetErrorHandler(nullptr);
        TIFFSetWarningHandler(nullptr);
        TIFF* tif = TIFFOpen(filepath.c_str(), "r");
        if (!tif) return false;
        uint32_t w = 0, h = 0;
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFClose(tif);
        *width = static_cast<int>(w);
        *height = static_cast<int>(h);
        return (*width > 0 && *height > 0);
    }

    // Default fast header reader (JPEG, PNG, BMP, etc.)
    int comp = 0;
    return (stbi_info(filepath.c_str(), width, height, &comp) && *width > 0 && *height > 0);
}

bool load_webp_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    size_t fsize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fsize == 0) return false;

    std::vector<uint8_t> data(fsize);
    file.read(reinterpret_cast<char*>(data.data()), fsize);

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return false;

    if (WebPGetFeatures(data.data(), data.size(), &config.input) != VP8_STATUS_OK) {
        return false;
    }

    int orig_w = config.input.width;
    int orig_h = config.input.height;
    if (orig_w <= 0 || orig_h <= 0) return false;

    int tw = target_w;
    int th = target_h;
    if (tw <= 0 || th <= 0) {
        tw = orig_w;
        th = orig_h;
    } else {
        double ar = static_cast<double>(orig_w) / orig_h;
        if (orig_w > orig_h) {
            th = std::max(1, static_cast<int>(tw / ar));
        } else {
            tw = std::max(1, static_cast<int>(th * ar));
        }
    }

    config.options.use_scaling = 1;
    config.options.scaled_width = tw;
    config.options.scaled_height = th;
    config.output.colorspace = MODE_RGB;

    rgb_out.resize(tw * th * 3);
    config.output.u.RGBA.rgba = rgb_out.data();
    config.output.u.RGBA.stride = tw * 3;
    config.output.u.RGBA.size = rgb_out.size();
    config.output.is_external_memory = 1;

    if (WebPDecode(data.data(), data.size(), &config) == VP8_STATUS_OK) {
        out_w = tw;
        out_h = th;
        return true;
    }

    return false;
}

bool load_tiff_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h) {
    TIFFSetErrorHandler(nullptr);
    TIFFSetWarningHandler(nullptr);

    TIFF* tif = TIFFOpen(filepath.c_str(), "r");
    if (!tif) return false;

    uint32_t w = 0, h = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

    if (w == 0 || h == 0) {
        TIFFClose(tif);
        return false;
    }

    std::vector<uint8_t> full_rgb;
    bool read_success = false;

    // Fast path: high-level RGBA converter (handles 8/16-bit int, palette, CMYK, etc.)
    std::vector<uint32_t> raster(w * h);
    if (TIFFReadRGBAImageOriented(tif, w, h, raster.data(), ORIENTATION_TOPLEFT, 0)) {
        full_rgb.resize(w * h * 3);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            uint32_t pixel = raster[i];
            full_rgb[i * 3 + 0] = static_cast<uint8_t>(TIFFGetR(pixel));
            full_rgb[i * 3 + 1] = static_cast<uint8_t>(TIFFGetG(pixel));
            full_rgb[i * 3 + 2] = static_cast<uint8_t>(TIFFGetB(pixel));
        }
        read_success = true;
    } else {
        // Fallback: raw scanline reader for floating-point (32/64-bit float) or unsupported sample depths
        uint16_t bps = 8, spp = 1, sampleformat = SAMPLEFORMAT_UINT, photometric = PHOTOMETRIC_MINISBLACK;
        TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bps);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
        TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &sampleformat);
        TIFFGetFieldDefaulted(tif, TIFFTAG_PHOTOMETRIC, &photometric);

        tmsize_t scanline_size = TIFFScanlineSize(tif);
        if (scanline_size > 0) {
            std::vector<uint8_t> scanline_buf(scanline_size);
            std::vector<double> raw_values(w * h * std::min<uint16_t>(spp, 4));
            bool ok = true;
            double min_val = 1e30, max_val = -1e30;

            for (uint32_t row = 0; row < h; ++row) {
                if (TIFFReadScanline(tif, scanline_buf.data(), row) < 0) {
                    ok = false;
                    break;
                }
                for (uint32_t col = 0; col < w; ++col) {
                    for (uint16_t s = 0; s < std::min<uint16_t>(spp, 4); ++s) {
                        double val = 0.0;
                        size_t pixel_idx = (col * spp + s);
                        if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                            if (bps == 64) {
                                val = reinterpret_cast<const double*>(scanline_buf.data())[pixel_idx];
                            } else if (bps == 32) {
                                val = reinterpret_cast<const float*>(scanline_buf.data())[pixel_idx];
                            }
                        } else if (sampleformat == SAMPLEFORMAT_INT) {
                            if (bps == 32) {
                                val = reinterpret_cast<const int32_t*>(scanline_buf.data())[pixel_idx];
                            } else if (bps == 16) {
                                val = reinterpret_cast<const int16_t*>(scanline_buf.data())[pixel_idx];
                            } else if (bps == 8) {
                                val = reinterpret_cast<const int8_t*>(scanline_buf.data())[pixel_idx];
                            }
                        } else { // UNSIGNED INT
                            if (bps == 32) {
                                val = reinterpret_cast<const uint32_t*>(scanline_buf.data())[pixel_idx];
                            } else if (bps == 16) {
                                val = reinterpret_cast<const uint16_t*>(scanline_buf.data())[pixel_idx];
                            } else if (bps == 8) {
                                val = reinterpret_cast<const uint8_t*>(scanline_buf.data())[pixel_idx];
                            }
                        }
                        raw_values[(row * w + col) * std::min<uint16_t>(spp, 4) + s] = val;
                        if (std::isfinite(val)) {
                            min_val = std::min(min_val, val);
                            max_val = std::max(max_val, val);
                        }
                    }
                }
            }

            if (ok) {
                full_rgb.resize(w * h * 3);
                double range = max_val - min_val;
                if (range <= 1e-9) range = 1.0;

                for (uint32_t row = 0; row < h; ++row) {
                    for (uint32_t col = 0; col < w; ++col) {
                        size_t src_idx = (row * w + col) * std::min<uint16_t>(spp, 4);
                        size_t dst_idx = (row * w + col) * 3;

                        if (spp == 1) {
                            double v = raw_values[src_idx];
                            uint8_t byte_val = 0;
                            if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                                if (min_val >= 0.0 && max_val <= 1.0) {
                                    byte_val = static_cast<uint8_t>(std::clamp(v * 255.0, 0.0, 255.0));
                                } else if (min_val >= 0.0 && max_val <= 255.0) {
                                    byte_val = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
                                } else {
                                    byte_val = static_cast<uint8_t>(std::clamp((v - min_val) / range * 255.0, 0.0, 255.0));
                                }
                            } else if (bps == 16) {
                                byte_val = static_cast<uint8_t>(std::clamp(v / 256.0, 0.0, 255.0));
                            } else if (bps == 32) {
                                byte_val = static_cast<uint8_t>(std::clamp(v / 16777216.0, 0.0, 255.0));
                            } else {
                                byte_val = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
                            }
                            if (photometric == PHOTOMETRIC_MINISWHITE) {
                                byte_val = 255 - byte_val;
                            }
                            full_rgb[dst_idx + 0] = byte_val;
                            full_rgb[dst_idx + 1] = byte_val;
                            full_rgb[dst_idx + 2] = byte_val;
                        } else if (spp >= 3) {
                            for (int c = 0; c < 3; ++c) {
                                double v = raw_values[src_idx + c];
                                uint8_t byte_val = 0;
                                if (sampleformat == SAMPLEFORMAT_IEEEFP) {
                                    if (min_val >= 0.0 && max_val <= 1.0) {
                                        byte_val = static_cast<uint8_t>(std::clamp(v * 255.0, 0.0, 255.0));
                                    } else if (min_val >= 0.0 && max_val <= 255.0) {
                                        byte_val = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
                                    } else {
                                        byte_val = static_cast<uint8_t>(std::clamp((v - min_val) / range * 255.0, 0.0, 255.0));
                                    }
                                } else if (bps == 16) {
                                    byte_val = static_cast<uint8_t>(std::clamp(v / 256.0, 0.0, 255.0));
                                } else {
                                    byte_val = static_cast<uint8_t>(std::clamp(v, 0.0, 255.0));
                                }
                                full_rgb[dst_idx + c] = byte_val;
                            }
                        }
                    }
                }
                read_success = true;
            }
        }
    }
    TIFFClose(tif);

    if (!read_success) {
        return false;
    }

    if (target_w > 0 && target_h > 0 && (static_cast<int>(w) != target_w || static_cast<int>(h) != target_h)) {
        double ar = static_cast<double>(w) / h;
        int tw = target_w, th = target_h;
        if (w > h) {
            th = std::max(1, static_cast<int>(tw / ar));
        } else {
            tw = std::max(1, static_cast<int>(th * ar));
        }
        rgb_out.resize(tw * th * 3);
        stbir_resize_uint8_linear(full_rgb.data(), w, h, 0, rgb_out.data(), tw, th, 0, STBIR_RGB);
        out_w = tw;
        out_h = th;
    } else {
        rgb_out = std::move(full_rgb);
        out_w = static_cast<int>(w);
        out_h = static_cast<int>(h);
    }

    return true;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
