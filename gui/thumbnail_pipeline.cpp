#include "thumbnail_pipeline.h"
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#include <filesystem>
#include "../third_party/stb/stb_image.h"
#include "../third_party/stb/stb_image_resize2.h"
#include "../utils.h"
#include <iostream>
#include <xxhash.h>

ThumbnailPipeline::ThumbnailPipeline(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue,
                                     const std::string& db_path)
    : update_queue_(update_queue), db_path_(db_path), stop_requested_(false) {
    if (!db_path_.empty()) {
        db_.open(db_path_);
    }
}

ThumbnailPipeline::~ThumbnailPipeline() {
    stop();
}

void ThumbnailPipeline::start(int num_workers) {
    stop_requested_ = false;
    pending_requests_ = 0;
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&ThumbnailPipeline::worker_thread, this);
    }
}

void ThumbnailPipeline::stop() {
    stop_requested_ = true;
    wake_cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
    workers_.clear();
}

void ThumbnailPipeline::set_generation(uint64_t gen) {
    current_generation_ = gen;
    // When the UI increments the generation, it means all previous urgent requests are obsolete.
    // Clear the queue to prevent memory leaks and queue spam during continuous resizing/zooming.
    ThumbRequest req;
    while (urgent_queue_.try_dequeue(req)) {
        pending_requests_--;
    }
}

uint64_t ThumbnailPipeline::get_generation() const {
    return current_generation_;
}

void ThumbnailPipeline::request_thumbnail(size_t image_index, const std::string& filepath, const std::string& hash, ThumbQuality quality, bool urgent, uint64_t generation, int layout_w, int layout_h) {
    ThumbRequest req{image_index, filepath, quality, hash, generation, layout_w, layout_h};
    if (urgent) {
        urgent_queue_.enqueue(req);
    } else {
        normal_queue_.enqueue(req);
    }
    pending_requests_++;
    wake_cv_.notify_one();
}

void ThumbnailPipeline::worker_thread() {
    while (!stop_requested_) {
        ThumbRequest req;
        bool got_req = false;

        if (urgent_queue_.try_dequeue(req)) {
            got_req = true;
        } else if (normal_queue_.try_dequeue(req)) {
            got_req = true;
        }

        if (got_req) {
            if (req.generation > 0 && req.generation < current_generation_) {
                pending_requests_--;
                continue;
            }
            pending_requests_--;
            process_request(req);
        } else {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                return stop_requested_ || pending_requests_ > 0;
            });
        }
    }
}

bool ThumbnailPipeline::process_request(const ThumbRequest& req) {
    try {
        std::string hash = req.hash;
        if (hash.empty() && db_.is_open()) {
            std::lock_guard<std::mutex> lock(db_.get_mutex());
            if (db_.begin_transaction()) {
                db_.get_hash_for_path(req.filepath, hash);
                db_.abort_transaction();
            }
        }

        std::vector<uint8_t> jpeg_data;
        bool target_found = false;
        if (!hash.empty() && db_.is_open()) {
            std::lock_guard<std::mutex> lock(db_.get_mutex());
            if (db_.begin_transaction()) {
                std::string key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));
                if (db_.get_key_data(key, jpeg_data)) {
                    target_found = true;
                }
                db_.abort_transaction();
            }
        }

        if (target_found && req.generation == 0) {
            // This is a low-priority background scan task, and the thumbnail is already in the DB.
            // The UI didn't explicitly request this RGB data, so we don't need to send it.
            // This prevents a race condition where a lagging background task overwrites a perfectly-sized UI thumbnail.
            return true;
        }

        int target_w = req.layout_w;
        int target_h = req.layout_h;
        if (target_w <= 0 || target_h <= 0) {
            target_w = static_cast<int>(req.target_quality);
            target_h = static_cast<int>(req.target_quality);
        }

        std::vector<uint8_t> rgb_out;
        if (target_found) {
            std::vector<uint8_t> rgb_decoded;
            int dec_w, dec_h;
            if (decode_jpeg(jpeg_data.data(), jpeg_data.size(), rgb_decoded, dec_w, dec_h)) {
                if (dec_w == target_w && dec_h == target_h) {
                    rgb_out = std::move(rgb_decoded);
                } else {
                    rgb_out.resize(target_w * target_h * 3);
                    stbir_resize_uint8_linear(rgb_decoded.data(), dec_w, dec_h, 0,
                                              rgb_out.data(), target_w, target_h, 0, STBIR_RGB);
                }
                update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
                    req.image_index, req.filepath, req.target_quality, rgb_out, target_w, target_h, req.generation
                ));
                return true;
            }
        }
        int w = 0, h = 0, channels = 0;
        unsigned char* img = nullptr;
        std::vector<uint8_t> rgb_decoded;

        // Fast-path: use libjpeg-turbo with downscaling for JPEG files
        std::string ext = std::filesystem::path(req.filepath).extension().string();
        for (char& c : ext) c = std::tolower(c);
        
        bool fast_decoded = false;
        if (ext == ".jpg" || ext == ".jpeg") {
            if (load_jpeg_scaled_file(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                fast_decoded = true;
            }
        }

        if (!fast_decoded) {
            img = stbi_load(req.filepath.c_str(), &w, &h, &channels, 3);
            if (!img) {
                // If it's an extreme PNG, try streaming thumbnail generation fallback
                if (ext == ".png" && generate_png_streaming(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                    fast_decoded = true;
                } else {
                    update_queue_.enqueue(UpdateEvent::make_thumb_failed(req.image_index, req.filepath, req.target_quality));
                    return false;
                }
            }
        }

        if (hash.empty() && img) {
            XXH64_hash_t hval = XXH64(img, w * h * 3, 0);
            char hash_str[17];
            snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hval);
            hash = hash_str;
        } else if (hash.empty() && fast_decoded) {
            XXH64_hash_t hval = XXH64(rgb_decoded.data(), w * h * 3, 0);
            char hash_str[17];
            snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hval);
            hash = hash_str;
        }
        double ar = static_cast<double>(w) / h;
        if (req.layout_w <= 0 || req.layout_h <= 0) {
            int tw = static_cast<int>(req.target_quality);
            if (w > h) {
                target_w = tw;
                target_h = static_cast<int>(tw / ar);
            } else {
                target_h = tw;
                target_w = static_cast<int>(tw * ar);
            }
        }

        target_h = std::max(1, target_h);
        target_w = std::max(1, target_w);

        std::vector<uint8_t> resized;
        if (fast_decoded && w == target_w && h == target_h) {
            resized = std::move(rgb_decoded);
        } else {
            resized.resize(target_w * target_h * 3);
            const uint8_t* source_data = fast_decoded ? rgb_decoded.data() : img;
            stbir_resize_uint8_linear(source_data, w, h, 0, resized.data(), target_w, target_h, 0, STBIR_RGB);
        }
        
        if (img) stbi_image_free(img);

        std::vector<uint8_t> jpeg_data_out = encode_jpeg(resized.data(), target_w, target_h, 90);

        if (db_.is_open() && !hash.empty()) {
            std::lock_guard<std::mutex> lock(db_.get_mutex());
            if (db_.begin_transaction()) {
                std::string key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));
                if (db_.store_key_data(key, jpeg_data_out)) {
                    db_.store_key_value(hash + ":path", req.filepath);
                    db_.store_key_value("file:" + req.filepath, hash);
                    db_.commit_transaction();
                } else {
                    db_.abort_transaction();
                }
            }
        }

        update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
            req.image_index, req.filepath, req.target_quality, resized, target_w, target_h, req.generation
        ));
        return true;
    } catch (...) {
        return false;
    }
}

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

bool ThumbnailPipeline::decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height) {
    if (!jpeg_data || jpeg_size == 0) return false;

    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);

    width = cinfo.output_width;
    height = cinfo.output_height;
    int channels = cinfo.output_components;
    
    if (channels != 3) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    rgb_data.resize(width * height * channels);
    int row_stride = width * channels;

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* row = rgb_data.data() + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

bool ThumbnailPipeline::load_jpeg_scaled_file(const std::string& filepath, int target_w, int target_h, std::vector<uint8_t>& rgb_data, int& out_w, int& out_h) {
    FILE* infile = fopen(filepath.c_str(), "rb");
    if (!infile) return false;

    // Check header
    unsigned char header[2];
    if (fread(header, 1, 2, infile) != 2 || header[0] != 0xFF || header[1] != 0xD8) {
        fclose(infile);
        return false;
    }
    rewind(infile);

    jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    
    // Simple setjmp error handling to avoid exit() on corruption
    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    
    // Calculate optimal scaling factor (1/1, 1/2, 1/4, 1/8)
    int scale = 1;
    while (scale < 8 && (cinfo.image_width / (scale * 2)) >= target_w && (cinfo.image_height / (scale * 2)) >= target_h) {
        scale *= 2;
    }
    
    cinfo.scale_num = 1;
    cinfo.scale_denom = scale;
    cinfo.out_color_space = JCS_RGB;
    
    jpeg_start_decompress(&cinfo);

    out_w = cinfo.output_width;
    out_h = cinfo.output_height;
    int channels = cinfo.output_components;
    
    if (channels != 3) {
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return false;
    }

    rgb_data.resize(out_w * out_h * channels);
    int row_stride = out_w * channels;

    while (cinfo.output_scanline < cinfo.output_height) {
        unsigned char* row = rgb_data.data() + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);
    return true;
}

bool ThumbnailPipeline::generate_png_streaming(const std::string& filepath, int max_w, int max_h, std::vector<uint8_t>& out_rgb, int& out_w, int& out_h) {
    FILE *fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;
    
    // Check if it's a PNG
    unsigned char header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return false;
    }
    
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) { fclose(fp); return false; }

    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); return false; }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    double ar = static_cast<double>(width) / height;
    out_w = max_w;
    out_h = max_h;
    if (width > height) out_h = static_cast<int>(max_w / ar);
    else out_w = static_cast<int>(max_h * ar);
    
    out_w = std::max(1, out_w);
    out_h = std::max(1, out_h);

    std::vector<uint64_t> accum_r(out_w * out_h, 0);
    std::vector<uint64_t> accum_g(out_w * out_h, 0);
    std::vector<uint64_t> accum_b(out_w * out_h, 0);
    std::vector<uint64_t> accum_count(out_w * out_h, 0);

    int row_bytes = png_get_rowbytes(png, info);
    std::vector<png_byte> row(row_bytes);

    for (int y = 0; y < height; y++) {
        png_read_row(png, row.data(), NULL);
        int ty = (y * out_h) / height;
        ty = std::min(ty, out_h - 1);
        
        int row_offset = ty * out_w;
        for (int x = 0; x < width; x++) {
            int tx = (x * out_w) / width;
            tx = std::min(tx, out_w - 1);
            
            int idx = row_offset + tx;
            accum_r[idx] += row[x * 4 + 0];
            accum_g[idx] += row[x * 4 + 1];
            accum_b[idx] += row[x * 4 + 2];
            accum_count[idx]++;
        }
    }

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    
    out_rgb.resize(out_w * out_h * 3);
    for (int i = 0; i < out_w * out_h; i++) {
        if (accum_count[i] > 0) {
            out_rgb[i * 3 + 0] = accum_r[i] / accum_count[i];
            out_rgb[i * 3 + 1] = accum_g[i] / accum_count[i];
            out_rgb[i * 3 + 2] = accum_b[i] / accum_count[i];
        } else {
            out_rgb[i * 3 + 0] = 0;
            out_rgb[i * 3 + 1] = 0;
            out_rgb[i * 3 + 2] = 0;
        }
    }

    return true;
}
