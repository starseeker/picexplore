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
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <xxhash.h>
#include "TinyEXIF.h"
#include <climits>

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

struct my_jpeg_enc_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void my_jpeg_enc_error_exit(j_common_ptr cinfo) {
    auto* myerr = reinterpret_cast<my_jpeg_enc_error_mgr*>(cinfo->err);
    longjmp(myerr->setjmp_buffer, 1);
}

// Utility functions
std::vector<uint8_t> encode_jpeg(const unsigned char* rgb_data, int width, int height, int quality) {
    if (!rgb_data || width <= 0 || height <= 0) return {};

    struct jpeg_compress_struct cinfo;
    struct my_jpeg_enc_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_jpeg_enc_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_compress(&cinfo);
        return {};
    }

    jpeg_create_compress(&cinfo);

    unsigned char* outbuffer = nullptr;
    unsigned long outsize = 0;
    jpeg_mem_dest(&cinfo, &outbuffer, &outsize);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, std::clamp(quality, 1, 100), TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    int row_stride = width * 3;
    while (cinfo.next_scanline < cinfo.image_height) {
        JSAMPROW row_pointer[1] = { const_cast<JSAMPROW>(&rgb_data[cinfo.next_scanline * row_stride]) };
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    std::vector<uint8_t> jpeg_data;
    if (outbuffer && outsize > 0) {
        jpeg_data.assign(outbuffer, outbuffer + outsize);
        free(outbuffer);
    }
    return jpeg_data;
}

bool is_image_file(const std::string& filepath) {
    auto ext = std::filesystem::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp" ||
            ext == ".tga" || ext == ".webp" || ext == ".tif" || ext == ".tiff");
}

bool is_cache_or_db_path(const std::string& path, const std::string& db_path) {
    if (path.empty()) return false;

    const char* override_dir = getenv("PICEXPLORE_CACHE_DIR");
    const char* xdg = getenv("XDG_CACHE_HOME");
    const char* home = getenv("HOME");
    const std::string cache = override_dir && *override_dir ? override_dir :
        xdg && *xdg ? (std::filesystem::path(xdg) / "picexplore").string() :
        home ? (std::filesystem::path(home) / ".cache/picexplore").string() : "/tmp/picexplore";
    if (path_is_within(path, cache) || path_is_within(path, "/tmp/picexplore")) return true;
    if (!db_path.empty() && (path == db_path || path == db_path + "-lock")) return true;
    std::string_view p = path;

    // Fast filename check without fs::path construction
    size_t last_slash = p.find_last_of("/\\");
    std::string_view filename = (last_slash == std::string_view::npos) ? p : p.substr(last_slash + 1);

    if (filename == "images.db" || filename == "images.db-lock" ||
        filename == "cache.db" || filename == "cache.db-lock" ||
        filename == "thumbnails.db" || filename == "thumbnails.db-lock" ||
        filename == "data.mdb" || filename == "lock.mdb" ||
        filename == ".picexplore" || filename == "picexplore") {
        return true;
    }

    return false;
}

#include <webp/decode.h>
#include <tiffio.h>
#include <fstream>

bool get_image_info(const std::string& filepath, int* width, int* height) {
    if (!width || !height) return false;
    const auto result = probe_image(filepath);
    *width = result.width; *height = result.height;
    return bool(result);
}

bool load_webp_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h, int* orig_w, int* orig_h) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) return false;
    size_t fsize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fsize == 0 || fsize > 128ULL * 1024 * 1024) return false;

    std::vector<uint8_t> data(fsize);
    if (!file.read(reinterpret_cast<char*>(data.data()), fsize)) return false;

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return false;

    if (WebPGetFeatures(data.data(), data.size(), &config.input) != VP8_STATUS_OK) {
        return false;
    }

    int orig_w_val = config.input.width;
    int orig_h_val = config.input.height;
    if (orig_w) *orig_w = orig_w_val;
    if (orig_h) *orig_h = orig_h_val;
    if (orig_w_val <= 0 || orig_h_val <= 0) return false;

    int tw = target_w;
    int th = target_h;
    if (tw <= 0 || th <= 0) {
        tw = orig_w_val;
        th = orig_h_val;
    } else {
        double ar = static_cast<double>(orig_w_val) / orig_h_val;
        if (orig_w_val > orig_h_val) {
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

bool load_tiff_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out, int& out_w, int& out_h, int* orig_w, int* orig_h) {

    TIFF* tif = TIFFOpen(filepath.c_str(), "r");
    if (!tif) return false;

    uint32_t w = 0, h = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);

    if (orig_w) *orig_w = static_cast<int>(w);
    if (orig_h) *orig_h = static_cast<int>(h);

    // The RGBA and floating-point TIFF paths require whole-image work buffers.
    if (w == 0 || h == 0 || uint64_t(w) * h > 16000000) {
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
        uint16_t planar = PLANARCONFIG_CONTIG;
        TIFFGetFieldDefaulted(tif, TIFFTAG_PLANARCONFIG, &planar);
        const uint64_t required_row = uint64_t(w) * spp * (bps / 8);
        if (scanline_size > 0 && planar == PLANARCONFIG_CONTIG && spp >= 1 && spp <= 4 &&
            (bps == 8 || bps == 16 || bps == 32 || bps == 64) && required_row <= uint64_t(scanline_size)) {
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

bool load_png_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_out,
                   int& out_w, int& out_h, int* orig_w, int* orig_h, const std::atomic<bool>* cancelled) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(fp); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(fp); return false; }
    // Keep owning C++ objects outside the libpng longjmp region.
    std::vector<uint8_t> row, full;
    std::vector<png_bytep> rows;
    std::vector<uint64_t> sums;
    auto close = [&] { png_destroy_read_struct(&png, &info, nullptr); fclose(fp); };
    if (setjmp(png_jmpbuf(png))) { close(); rgb_out.clear(); return false; }
    png_init_io(png, fp);
    png_read_info(png, info);
    const int width = png_get_image_width(png, info), height = png_get_image_height(png, info);
    if (width <= 0 || height <= 0 || width > 1000000 || height > 1000000) { close(); return false; }
    if (orig_w) *orig_w = width;
    if (orig_h) *orig_h = height;
    const auto color = png_get_color_type(png, info);
    if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png, info) < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if ((color & PNG_COLOR_MASK_ALPHA) || png_get_valid(png, info, PNG_INFO_tRNS)) png_set_strip_alpha(png);
    const bool interlaced = png_get_interlace_type(png, info) != PNG_INTERLACE_NONE;
    if (interlaced) png_set_interlace_handling(png);
    png_read_update_info(png, info);
    const size_t row_bytes = png_get_rowbytes(png, info);
    if (row_bytes != size_t(width) * 3) { close(); return false; }
    double scale = target_w > 0 && target_h > 0 ?
        std::min({1.0, double(target_w) / width, double(target_h) / height}) : 1.0;
    out_w = std::max(1, int(width * scale)); out_h = std::max(1, int(height * scale));
    if (uint64_t(out_w) * out_h > 64000000 || (interlaced && uint64_t(width) * height > 32000000)) {
        close(); return false;
    }
    rgb_out.resize(size_t(out_w) * out_h * 3);
    if (interlaced) {
        full.resize(row_bytes * height);
        rows.resize(height);
        for (int y = 0; y < height; ++y) rows[y] = full.data() + size_t(y) * row_bytes;
        png_read_image(png, rows.data());
        stbir_resize_uint8_linear(full.data(), width, height, 0, rgb_out.data(), out_w, out_h, 0, STBIR_RGB);
    } else {
        row.resize(row_bytes);
        sums.resize(size_t(out_w) * 4);
        int output_y = 0;
        auto finish_row = [&] {
            for (int x = 0; x < out_w; ++x) {
                const auto* sum = sums.data() + size_t(x) * 4;
                for (int c = 0; c < 3; ++c)
                    rgb_out[(size_t(output_y) * out_w + x) * 3 + c] = sum[3] ? sum[c] / sum[3] : 0;
            }
            std::fill(sums.begin(), sums.end(), 0);
        };
        for (int y = 0; y < height; ++y) {
            if (cancelled && cancelled->load()) { close(); rgb_out.clear(); return false; }
            const int ty = int(int64_t(y) * out_h / height);
            if (ty != output_y) { finish_row(); output_y = ty; }
            png_read_row(png, row.data(), nullptr);
            for (int x = 0; x < width; ++x) {
                const int tx = int(int64_t(x) * out_w / width);
                auto* sum = sums.data() + size_t(tx) * 4;
                for (int c = 0; c < 3; ++c) sum[c] += row[size_t(x) * 3 + c];
                ++sum[3];
            }
        }
        finish_row();
    }
    png_read_end(png, info);
    close();
    return true;
}

bool compute_file_hash(const std::string& filepath, std::string& hash_out) {
    FILE* fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;

    XXH3_state_t* state = XXH3_createState();
    if (!state) {
        fclose(fp);
        return false;
    }
    XXH3_128bits_reset(state);

    char buf[65536];
    size_t bytes_read = 0;
    while ((bytes_read = fread(buf, 1, sizeof(buf), fp)) > 0) {
        XXH3_128bits_update(state, buf, bytes_read);
    }
    fclose(fp);

    XXH128_hash_t hash = XXH3_128bits_digest(state);
    XXH3_freeState(state);

    char hash_str[33];
    snprintf(hash_str, sizeof(hash_str), "%016llx%016llx",
             (unsigned long long)hash.high64, (unsigned long long)hash.low64);
    hash_out = hash_str;
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


#include <sys/stat.h>

bool read_file_stamp(const std::string& path, FileStamp& stamp) {
    stamp = {};
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) return false;
    stamp.device = info.st_dev;
    stamp.inode = info.st_ino;
    stamp.size = info.st_size;
#if defined(__APPLE__)
    stamp.modified_ns = int64_t(info.st_mtimespec.tv_sec) * 1000000000LL + info.st_mtimespec.tv_nsec;
    stamp.changed_ns = int64_t(info.st_ctimespec.tv_sec) * 1000000000LL + info.st_ctimespec.tv_nsec;
#else
    stamp.modified_ns = int64_t(info.st_mtim.tv_sec) * 1000000000LL + info.st_mtim.tv_nsec;
    stamp.changed_ns = int64_t(info.st_ctim.tv_sec) * 1000000000LL + info.st_ctim.tv_nsec;
#endif
    stamp.valid = true;
    return true;
}

std::string normalize_path(const std::string& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    return error ? std::filesystem::path(path).lexically_normal().string() : absolute.lexically_normal().string();
}

bool path_is_within(const std::string& path, const std::string& directory) {
    const auto p = std::filesystem::path(path).lexically_normal();
    const auto root = std::filesystem::path(directory).lexically_normal();
    auto pi = p.begin();
    for (auto ri = root.begin(); ri != root.end(); ++ri, ++pi) {
        if (pi == p.end() || *pi != *ri) return false;
    }
    return true;
}

struct DecoderJpegError {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
    const char* filepath = nullptr;
};

static void decoder_jpeg_error(j_common_ptr cinfo) {
    DecoderJpegError* myerr = (DecoderJpegError*)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG error: %s\n", buffer);
    }
    longjmp(myerr->setjmp_buffer, 1);
}

static void decoder_jpeg_message(j_common_ptr cinfo) {
    DecoderJpegError* myerr = (DecoderJpegError*)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG: %s\n", buffer);
    }
}

bool load_jpeg_scaled(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_data, int& out_w, int& out_h, int* orig_w, int* orig_h) {
    FILE* infile = fopen(filepath.c_str(), "rb");
    if (!infile) return false;

    // Check header
    unsigned char header[2];
    if (fread(header, 1, 2, infile) != 2 || header[0] != 0xFF || header[1] != 0xD8) {
        fclose(infile);
        return false;
    }
    rewind(infile);

    jpeg_decompress_struct cinfo{};
    struct DecoderJpegError jerr;
    jerr.filepath = filepath.c_str();
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = decoder_jpeg_error;
    jerr.pub.output_message = decoder_jpeg_message;

    // Simple setjmp error handling to avoid exit() on corruption
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);

    if (orig_w) *orig_w = cinfo.image_width;
    if (orig_h) *orig_h = cinfo.image_height;

    // Calculate optimal scaling factor (1/1, 1/2, 1/4, 1/8)
    if (target_w <= 0) target_w = cinfo.image_width;
    if (target_h <= 0) target_h = cinfo.image_height;
    int scale = 1;
    while (scale < 8 && (cinfo.image_width / (scale * 2)) >= target_w && (cinfo.image_height / (scale * 2)) >= target_h) {
        scale *= 2;
    }

    cinfo.scale_num = 1;
    cinfo.scale_denom = scale;
    bool is_cmyk = (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK);
    if (is_cmyk) {
        cinfo.out_color_space = JCS_CMYK;
    } else {
        cinfo.out_color_space = JCS_RGB;
    }

    // Fast decoding parameters suitable for thumbnails
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = FALSE;

    jpeg_calc_output_dimensions(&cinfo);
    if (uint64_t(cinfo.output_width) * cinfo.output_height > 64000000) {
        jpeg_destroy_decompress(&cinfo); fclose(infile); return false;
    }
    jpeg_start_decompress(&cinfo);

    out_w = cinfo.output_width;
    out_h = cinfo.output_height;
    int channels = cinfo.output_components;

    rgb_data.resize(size_t(out_w) * out_h * channels);
    int row_stride = out_w * channels;

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* row = rgb_data.data() + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    if (is_cmyk && channels == 4) {
        std::vector<uint8_t> rgb_converted(out_w * out_h * 3);
        for (size_t i = 0; i < (size_t)out_w * out_h; i++) {
            uint8_t c = rgb_data[i * 4 + 0];
            uint8_t m = rgb_data[i * 4 + 1];
            uint8_t y = rgb_data[i * 4 + 2];
            uint8_t k = rgb_data[i * 4 + 3];
            rgb_converted[i * 3 + 0] = (c * k) / 255;
            rgb_converted[i * 3 + 1] = (m * k) / 255;
            rgb_converted[i * 3 + 2] = (y * k) / 255;
        }
        rgb_data = std::move(rgb_converted);
    } else if (channels == 1) {
        std::vector<uint8_t> rgb_converted(out_w * out_h * 3);
        for (size_t i = 0; i < (size_t)out_w * out_h; i++) {
            uint8_t g = rgb_data[i];
            rgb_converted[i * 3 + 0] = g;
            rgb_converted[i * 3 + 1] = g;
            rgb_converted[i * 3 + 2] = g;
        }
        rgb_data = std::move(rgb_converted);
    }

    return true;
}


ImageProbe probe_image(const std::string& path) {
    ImageProbe result;
    std::ifstream file(path, std::ios::binary);
    unsigned char header[64]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    const auto size = file.gcount();
    int channels = 0;
    if (size >= 8 && !png_sig_cmp(header, 0, 8)) {
        result.format = ImageFormat::PNG;
        result.supports_tiles = size >= 29 && header[28] == PNG_INTERLACE_NONE;
    } else if (size >= 2 && header[0] == 0xff && header[1] == 0xd8) {
        result.format = ImageFormat::JPEG;
        file.clear(); file.seekg(0);
        TinyEXIF::EXIFInfo exif(file);
        if (exif.Orientation >= 1 && exif.Orientation <= 8) result.orientation = exif.Orientation;
    } else if (size >= 12 && !memcmp(header, "RIFF", 4) && !memcmp(header + 8, "WEBP", 4)) {
        result.format = ImageFormat::WEBP;
        WebPGetInfo(header, size, &result.width, &result.height);
    } else if (size >= 4 && ((!memcmp(header, "II", 2) && (header[2] == 42 || header[2] == 43)) ||
                            (!memcmp(header, "MM", 2) && (header[3] == 42 || header[3] == 43)))) {
        result.format = ImageFormat::TIFF;
        TIFF* tif = TIFFOpen(path.c_str(), "r");
        if (tif) {
            uint32_t w = 0, h = 0;
            TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
            TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
            TIFFClose(tif);
            if (w <= INT_MAX && h <= INT_MAX) { result.width = w; result.height = h; }
        }
    } else result.format = ImageFormat::OTHER;
    if (result.format != ImageFormat::WEBP && result.format != ImageFormat::TIFF)
        stbi_info(path.c_str(), &result.width, &result.height, &channels);
    if (result.orientation >= 5) std::swap(result.width, result.height);
    if (result.width <= 0 || result.height <= 0 || result.width > 1000000 || result.height > 1000000) {
        result.width = result.height = 0;
        result.supports_tiles = false;
        result.error = "Cannot identify the image header: " + path;
    }
    return result;
}

void orient_rgb(std::vector<uint8_t>& rgb, int& width, int& height, int orientation) {
    if (orientation <= 1 || orientation > 8 || width <= 0 || height <= 0) return;
    const int nw = orientation >= 5 ? height : width, nh = orientation >= 5 ? width : height;
    std::vector<uint8_t> output(rgb.size());
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        int dx = x, dy = y;
        switch (orientation) {
        case 2: dx = width - 1 - x; break;
        case 3: dx = width - 1 - x; dy = height - 1 - y; break;
        case 4: dy = height - 1 - y; break;
        case 5: dx = y; dy = x; break;
        case 6: dx = height - 1 - y; dy = x; break;
        case 7: dx = height - 1 - y; dy = width - 1 - x; break;
        case 8: dx = y; dy = width - 1 - x; break;
        }
        std::copy_n(rgb.data() + (size_t(y) * width + x) * 3, 3,
                    output.data() + (size_t(dy) * nw + dx) * 3);
    }
    rgb = std::move(output); width = nw; height = nh;
}

DecodeResult decode_image(const std::string& path, int max_w, int max_h, const std::atomic<bool>* cancelled) {
    DecodeResult result;
    auto stopped = [&] { return cancelled && cancelled->load(); };
    if (stopped()) { result.status = DecodeStatus::CANCELLED; return result; }
    FileStamp before, after;
    if (!read_file_stamp(path, before)) { result.error = "Cannot read " + path; return result; }
    result.source = probe_image(path);
    if (!result.source) { result.error = result.source.error; return result; }
    const auto& source = result.source;
    const uint64_t pixels = uint64_t(source.width) * source.height;
    const bool scaled = max_w > 0 && max_h > 0;
    // PNG can stream rows and JPEG/WebP support native reduction. Other decoders
    // require full-size work buffers and must reject unreasonable allocations.
    if ((!scaled && pixels > 64000000) || (source.format == ImageFormat::TIFF && pixels > 16000000) ||
        (source.format == ImageFormat::OTHER && pixels > 32000000)) {
        result.status = DecodeStatus::TOO_LARGE;
        result.error = "This image exceeds the decoder memory limit; a smaller copy is required.";
        return result;
    }
    try {
        bool ok = false;
        int tw = max_w, th = max_h;
        if (source.orientation >= 5) std::swap(tw, th);
        switch (source.format) {
        case ImageFormat::JPEG: ok = load_jpeg_scaled(path, tw, th, result.rgb, result.width, result.height); break;
        case ImageFormat::PNG: ok = load_png_file(path, tw, th, result.rgb, result.width, result.height, nullptr, nullptr, cancelled); break;
        case ImageFormat::WEBP: ok = load_webp_file(path, tw, th, result.rgb, result.width, result.height); break;
        case ImageFormat::TIFF: ok = load_tiff_file(path, tw, th, result.rgb, result.width, result.height); break;
        default: {
            int channels;
            auto* data = stbi_load(path.c_str(), &result.width, &result.height, &channels, 3);
            if (data) {
                result.rgb.assign(data, data + size_t(result.width) * result.height * 3);
                stbi_image_free(data); ok = true;
            }
        }
        }
        if (ok) {
            orient_rgb(result.rgb, result.width, result.height, source.orientation);
            if (scaled && (result.width > max_w || result.height > max_h)) {
                const double scale = std::min(double(max_w) / result.width, double(max_h) / result.height);
                const int w = std::max(1, int(result.width * scale)), h = std::max(1, int(result.height * scale));
                std::vector<uint8_t> resized(size_t(w) * h * 3);
                ok = stbir_resize_uint8_linear(result.rgb.data(), result.width, result.height, 0,
                    resized.data(), w, h, 0, STBIR_RGB) != nullptr;
                result.rgb = std::move(resized); result.width = w; result.height = h;
            }
        }
        if (stopped()) result.status = DecodeStatus::CANCELLED;
        else if (!read_file_stamp(path, after) || before != after) result.status = DecodeStatus::SOURCE_CHANGED;
        else if (ok && result.width > 0 && result.height > 0 && result.rgb.size() == size_t(result.width) * result.height * 3)
            result.status = DecodeStatus::READY;
        if (!result) {
            result.rgb.clear();
            result.error = result.status == DecodeStatus::SOURCE_CHANGED ? "Image changed while it was being decoded; retry." :
                "Cannot decode " + path + ". It may be damaged, incomplete, or too large for this decoder.";
        }
    } catch (const std::exception& e) { result.rgb.clear(); result.error = e.what(); }
    return result;
}
