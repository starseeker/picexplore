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
    wake_cv_.notify_all();
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
            if (req.generation > 0 && current_generation_ > req.generation + 2) {
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
            db_.get_hash_for_path_concurrent(req.filepath, hash);
        }

        std::vector<uint8_t> jpeg_data;
        bool target_found = false;
        ThumbQuality found_quality = ThumbQuality::NONE;

        if (!hash.empty() && db_.is_open()) {
            if (req.target_quality == ThumbQuality::SQUARE_128 || req.target_quality == ThumbQuality::SQUARE_64) {
                std::string sq_key = (req.target_quality == ThumbQuality::SQUARE_64) ? (hash + ":sq64") : (hash + ":sq128");
                if (db_.get_key_data_concurrent(sq_key, jpeg_data)) {
                    target_found = true;
                    found_quality = req.target_quality;
                } else if (db_.get_key_data_concurrent(hash + ":sq128", jpeg_data)) {
                    target_found = true;
                    found_quality = ThumbQuality::SQUARE_128;
                } else if (db_.get_key_data_concurrent(hash + ":sq64", jpeg_data)) {
                    target_found = true;
                    found_quality = ThumbQuality::SQUARE_64;
                }
            }

            if (!target_found) {
                // 1. Try exact target quality first
                std::string key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));
                if (db_.get_key_data_concurrent(key, jpeg_data)) {
                    target_found = true;
                    found_quality = req.target_quality;
                } else {
                    // 2. Try best available quality in DB: check sizes from largest down to smallest
                    std::vector<int> candidate_sizes = {2048, 1024, 512, 256, 128, 64, 32};
                    for (int sz : candidate_sizes) {
                        std::string cand_key = hash + ":" + std::to_string(sz);
                        if (db_.get_key_data_concurrent(cand_key, jpeg_data)) {
                            target_found = true;
                            found_quality = static_cast<ThumbQuality>(sz);
                            break;
                        }
                    }
                }
            }
        }

        int target_w = req.layout_w;
        int target_h = req.layout_h;
        if (target_w <= 0 || target_h <= 0) {
            target_w = static_cast<int>(req.target_quality);
            target_h = static_cast<int>(req.target_quality);
        }

        if (target_found) {
            std::vector<uint8_t> rgb_decoded;
            int dec_w = 0, dec_h = 0;
            if (decode_jpeg(jpeg_data.data(), jpeg_data.size(), rgb_decoded, dec_w, dec_h)) {
                update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
                    req.image_index, req.filepath, hash, found_quality, std::vector<uint8_t>(rgb_decoded), dec_w, dec_h, req.generation
                ));
                
                if (req.target_quality == ThumbQuality::SQUARE_128 || req.target_quality == ThumbQuality::SQUARE_64) {
                    return true;
                }

                // If the cached thumbnail in LMDB is already of equal or higher quality, we're done!
                if (static_cast<int>(found_quality) >= static_cast<int>(req.target_quality)) {
                    return true;
                }
                // Otherwise (found_quality < req.target_quality): we emitted the fast placeholder,
                // now continue below to generate the sharp full target_quality from the original file!
            }
        }
        int w = 0, h = 0, channels = 0;
        unsigned char* img = nullptr;
        std::vector<uint8_t> rgb_decoded;

        // Fast-path: use specialized decoders for JPEG, WebP, TIFF
        std::string ext = std::filesystem::path(req.filepath).extension().string();
        for (char& c : ext) c = std::tolower(c);
        
        bool fast_decoded = false;
        if (ext == ".jpg" || ext == ".jpeg") {
            if (load_jpeg_scaled_file(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                fast_decoded = true;
            }
        } else if (ext == ".webp") {
            if (load_webp_file(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                fast_decoded = true;
            }
        } else if (ext == ".tif" || ext == ".tiff") {
            if (load_tiff_file(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                fast_decoded = true;
            }
        } else if (ext == ".png") {
            if (load_png_file(req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                fast_decoded = true;
            }
        }

        if (!fast_decoded) {
            int info_w, info_h, info_c;
            bool is_massive_png = false;
            if (ext == ".png" && stbi_info(req.filepath.c_str(), &info_w, &info_h, &info_c)) {
                if ((long long)info_w * info_h > 25000000LL) {
                    is_massive_png = true;
                }
            }

            if (is_massive_png) {
                // generate_png_streaming will emit progress as it decodes row by row
                if (generate_png_streaming(req.image_index, req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                    fast_decoded = true;
                }
            } else {
                if (static_cast<int>(req.target_quality) >= 2048) {
                    update_queue_.enqueue(UpdateEvent::make_thumb_generation_progress(req.image_index, req.filepath, 0));
                }
                img = stbi_load(req.filepath.c_str(), &w, &h, &channels, 3);
            }
            if (!img && !fast_decoded) {
                if (ext == ".png" && generate_png_streaming(req.image_index, req.filepath, target_w, target_h, rgb_decoded, w, h)) {
                    fast_decoded = true;
                } else {
                    update_queue_.enqueue(UpdateEvent::make_thumb_failed(req.image_index, req.filepath, req.target_quality));
                    return false;
                }
            }
        }

        if (hash.empty() && img) {
            XXH128_hash_t hval = XXH3_128bits(img, w * h * 3);
            char hash_str[33];
            snprintf(hash_str, sizeof(hash_str), "%016llx%016llx",
                     (unsigned long long)hval.high64, (unsigned long long)hval.low64);
            hash = hash_str;
        } else if (hash.empty() && fast_decoded) {
            XXH128_hash_t hval = XXH3_128bits(rgb_decoded.data(), w * h * 3);
            char hash_str[33];
            snprintf(hash_str, sizeof(hash_str), "%016llx%016llx",
                     (unsigned long long)hval.high64, (unsigned long long)hval.low64);
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

        const uint8_t* source_data = fast_decoded ? rgb_decoded.data() : img;
        std::vector<uint8_t> resized;
        std::vector<uint8_t> sq128_jpeg;

        if (req.target_quality == ThumbQuality::SQUARE_128 || req.target_quality == ThumbQuality::SQUARE_64) {
            int sq_target = (req.target_quality == ThumbQuality::SQUARE_64) ? 64 : 128;
            target_w = sq_target;
            target_h = sq_target;

            int sq_dim = std::min(w, h);
            int crop_x = (w - sq_dim) / 2;
            int crop_y = (h - sq_dim) / 2;
            const uint8_t* crop_src = source_data + (crop_y * w + crop_x) * 3;

            resized.resize(sq_target * sq_target * 3);
            stbir_resize_uint8_linear(crop_src, sq_dim, sq_dim, w * 3,
                                     resized.data(), sq_target, sq_target, 0, STBIR_RGB);
        } else {
            if (fast_decoded && w == target_w && h == target_h) {
                resized = rgb_decoded;
            } else {
                resized.resize(target_w * target_h * 3);
                stbir_resize_uint8_linear(source_data, w, h, 0, resized.data(), target_w, target_h, 0, STBIR_RGB);
            }

            if (source_data && w > 0 && h > 0) {
                int sq_dim = std::min(w, h);
                int crop_x = (w - sq_dim) / 2;
                int crop_y = (h - sq_dim) / 2;
                const uint8_t* crop_src = source_data + (crop_y * w + crop_x) * 3;
                std::vector<uint8_t> sq128_rgb(128 * 128 * 3);
                stbir_resize_uint8_linear(crop_src, sq_dim, sq_dim, w * 3,
                                         sq128_rgb.data(), 128, 128, 0, STBIR_RGB);
                sq128_jpeg = encode_jpeg(sq128_rgb.data(), 128, 128, 85);
            }
        }
        
        if (img) stbi_image_free(img);
        img = nullptr;

        std::vector<uint8_t> jpeg_data_out = encode_jpeg(resized.data(), target_w, target_h, 85);

        if (db_.is_open() && !hash.empty()) {
            std::lock_guard<std::mutex> lock(db_.get_mutex());
            if (db_.begin_transaction()) {
                std::string key;
                if (req.target_quality == ThumbQuality::SQUARE_128) key = hash + ":sq128";
                else if (req.target_quality == ThumbQuality::SQUARE_64) key = hash + ":sq64";
                else key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));

                if (db_.store_key_data(key, jpeg_data_out)) {
                    db_.add_path_for_hash(hash, req.filepath);
                    db_.store_key_value("file:" + req.filepath, hash);

                    // Also ensure square thumbnail is cached in DB if we didn't just write it
                    if (!sq128_jpeg.empty()) {
                        std::vector<uint8_t> sq_check;
                        if (!db_.get_key_data(hash + ":sq128", sq_check)) {
                            db_.store_key_data(hash + ":sq128", sq128_jpeg);
                        }
                    }

                    // Ensure metadata is stored in DB
                    ImageMetadata meta;
                    if (!db_.get_image_metadata(hash, meta)) {
                        uint64_t file_size = 0, file_timestamp = 0;
                        try {
                            file_size = std::filesystem::file_size(req.filepath);
                            file_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                std::filesystem::last_write_time(req.filepath).time_since_epoch()).count();
                        } catch (...) {}
                        meta.file_size = file_size;
                        meta.file_timestamp = file_timestamp;
                        meta.orig_width = w;
                        meta.orig_height = h;
                        db_.store_image_metadata(hash, meta);
                    }

                    db_.commit_transaction();
                } else {
                    db_.abort_transaction();
                }
            }
        }

        update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
            req.image_index, req.filepath, hash, req.target_quality, std::move(resized), target_w, target_h, req.generation
        ));
        return true;
    } catch (...) {
        return false;
    }
}

struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
    const char* filepath = nullptr;
};

static void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG error: %s\n", buffer);
    }
    longjmp(myerr->setjmp_buffer, 1);
}

static void my_output_message(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG: %s\n", buffer);
    }
}

bool ThumbnailPipeline::decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height) {
    if (!jpeg_data || jpeg_size == 0) return false;

    struct jpeg_decompress_struct cinfo;
    struct my_error_mgr jerr;
    jerr.filepath = nullptr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, (unsigned char*)jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    
    cinfo.out_color_space = JCS_RGB;
    
    // Fast decoding parameters suitable for thumbnails
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = FALSE;
    
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
    jerr.filepath = filepath.c_str();
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = my_error_exit;
    jerr.pub.output_message = my_output_message;
    
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
    bool is_cmyk = (cinfo.jpeg_color_space == JCS_CMYK || cinfo.jpeg_color_space == JCS_YCCK);
    if (is_cmyk) {
        cinfo.out_color_space = JCS_CMYK;
    } else {
        cinfo.out_color_space = JCS_RGB;
    }
    
    // Fast decoding parameters suitable for thumbnails
    cinfo.dct_method = JDCT_IFAST;
    cinfo.do_fancy_upsampling = FALSE;
    
    jpeg_start_decompress(&cinfo);

    out_w = cinfo.output_width;
    out_h = cinfo.output_height;
    int channels = cinfo.output_components;
    
    rgb_data.resize(out_w * out_h * channels);
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

bool ThumbnailPipeline::generate_png_streaming(size_t image_index, const std::string& filepath, int max_w, int max_h, std::vector<uint8_t>& out_rgb, int& out_w, int& out_h) {
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

    int last_percent = -1;

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
        
        int percent = (y * 100) / height;
        if (percent != last_percent && percent % 5 == 0) {
            update_queue_.enqueue(UpdateEvent::make_thumb_generation_progress(image_index, filepath, percent));
            last_percent = percent;
        }
    }
    
    update_queue_.enqueue(UpdateEvent::make_thumb_generation_progress(image_index, filepath, 100));

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
