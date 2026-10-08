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
                                     const std::string& db_path, uint64_t session)
    : update_queue_(update_queue, session), db_path_(db_path), stop_requested_(false) {
    if (!db_path_.empty()) {
        if (!db_.open(db_path_)) {
            update_queue_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::DATABASE_ERROR,
                "Could not open the thumbnail cache. Check its location and permissions, then rescan."));
        }
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

void ThumbnailPipeline::request_thumbnail(size_t image_index, const std::string& filepath, const std::string& hash, ThumbQuality quality, bool urgent, uint64_t generation, int layout_w, int layout_h, uint64_t revision) {
    ThumbRequest req{image_index, filepath, quality, hash, generation, layout_w, layout_h, {update_queue_.session(), image_index, revision, 0}};
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
        bool is_upgrade = false;

        if (urgent_queue_.try_dequeue(req)) {
            got_req = true;
            is_upgrade = false;
        } else if (normal_queue_.try_dequeue(req)) {
            got_req = true;
            is_upgrade = false;
        } else if (upgrade_queue_.try_dequeue(req)) {
            got_req = true;
            is_upgrade = true;
        }

        if (got_req) {
            if (req.generation > 0 && current_generation_ > req.generation + 1) {
                pending_requests_--;
                continue;
            }
            pending_requests_--;
            process_request(req, is_upgrade);
        } else {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait_for(lock, std::chrono::milliseconds(50), [this]() {
                return stop_requested_ || pending_requests_ > 0;
            });
        }
    }
}

bool ThumbnailPipeline::process_request(const ThumbRequest& req, bool is_upgrade) {
    try {
        FileStamp before, after;
        if (!read_file_stamp(req.filepath, before)) return false;
        std::string hash;
        if (!db_.is_open() || !db_.get_valid_hash_for_path(req.filepath, hash)) {
            if (!compute_file_hash(req.filepath, hash)) return false;
        }
        // A request names a source version. Never decode new bytes under an old hash.
        if ((!req.hash.empty() && req.hash != hash) ||
            !read_file_stamp(req.filepath, after) || before != after) return false;

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
                    if (!target_found) {
                        // Check fast square thumbnail cache as preliminary placeholder
                        if (db_.get_key_data_concurrent(hash + ":sq128", jpeg_data)) {
                            target_found = true;
                            found_quality = ThumbQuality::SQUARE_128;
                        } else if (db_.get_key_data_concurrent(hash + ":sq64", jpeg_data)) {
                            target_found = true;
                            found_quality = ThumbQuality::SQUARE_64;
                        }
                    }
                }
            }
        }

        int target_w = req.layout_w;
        int target_h = req.layout_h;
        if (target_w <= 0 || target_h <= 0) {
            target_w = req.target_quality == ThumbQuality::SQUARE_64 ? 64 :
                req.target_quality == ThumbQuality::SQUARE_128 ? 128 : static_cast<int>(req.target_quality);
            target_h = target_w;
        }

        if (target_found && !is_upgrade) {
            std::vector<uint8_t> rgb_decoded;
            int dec_w = 0, dec_h = 0;
            if (decode_jpeg(jpeg_data.data(), jpeg_data.size(), rgb_decoded, dec_w, dec_h)) {
                if (!read_file_stamp(req.filepath, after) || before != after) return false;
                update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
                    req.image_index, req.filepath, hash, found_quality, std::vector<uint8_t>(rgb_decoded), dec_w, dec_h, req.generation
                ), req.token);

                if (is_square_quality(req.target_quality)) {
                    if (static_cast<int>(found_quality) >= static_cast<int>(req.target_quality)) {
                        return true;
                    }
                } else {
                    // Cached square thumbnails must not satisfy non-square layout requests
                    if (!is_square_quality(found_quality) && static_cast<int>(found_quality) >= static_cast<int>(req.target_quality)) {
                        return true;
                    }
                }

                // Enqueue upgrade to background upgrade queue so other urgent fast-cache requests are never blocked
                upgrade_queue_.enqueue(req);
                pending_requests_++;
                wake_cv_.notify_one();
                return true;
            }
        }
        auto decoded = decode_image(req.filepath, target_w, target_h, &stop_requested_);
        if (!decoded) {
            if (decoded.status != DecodeStatus::CANCELLED) {
                auto event = UpdateEvent::make_thumb_failed(req.image_index, req.filepath, req.target_quality);
                event.message = decoded.error;
                update_queue_.enqueue(std::move(event), req.token);
            }
            return false;
        }
        int w = decoded.width, h = decoded.height;
        int orig_w = decoded.source.width, orig_h = decoded.source.height;
        auto& rgb_decoded = decoded.rgb;
        const bool fast_decoded = true;
        unsigned char* img = nullptr;

        int full_w = (orig_w > 0) ? orig_w : w;
        int full_h = (orig_h > 0) ? orig_h : h;
        double ar = (full_h > 0) ? (static_cast<double>(full_w) / full_h) : 1.0;
        if (req.layout_w <= 0 || req.layout_h <= 0) {
            int tw = static_cast<int>(req.target_quality);
            if (full_w > full_h) {
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

        if (!read_file_stamp(req.filepath, after) || before != after || stop_requested_) return false;
        std::vector<uint8_t> jpeg_data_out = encode_jpeg(resized.data(), target_w, target_h, 85);

        if (db_.is_open() && !hash.empty()) {
            DatabaseManager::WriteTransaction transaction(db_);
            bool cached = false;
            if (transaction) {
                std::string key;
                if (req.target_quality == ThumbQuality::SQUARE_128) key = hash + ":sq128";
                else if (req.target_quality == ThumbQuality::SQUARE_64) key = hash + ":sq64";
                else key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));

                if (db_.store_key_data(key, jpeg_data_out)) {
                    // Only the reconciler owns path records; jobs write immutable content caches.

                    // Also ensure square thumbnail is cached in DB if we didn't just write it
                    if (!sq128_jpeg.empty()) {
                        std::vector<uint8_t> sq_check;
                        if (!db_.get_key_data(hash + ":sq128", sq_check)) {
                            db_.store_key_data(hash + ":sq128", sq128_jpeg);
                        }
                    }

                    // Ensure metadata is stored in DB with true original image dimensions
                    ImageMetadata meta;
                    bool meta_exists = db_.get_image_metadata(hash, meta);
                    int true_w = (orig_w > 0) ? orig_w : w;
                    int true_h = (orig_h > 0) ? orig_h : h;
                    if (!meta_exists || (true_w > meta.orig_width || true_h > meta.orig_height)) {
                        uint64_t file_size = meta_exists ? meta.file_size : 0;
                        uint64_t file_timestamp = meta_exists ? meta.file_timestamp : 0;
                        if (!meta_exists || file_size == 0) {
                            try {
                                file_size = std::filesystem::file_size(req.filepath);
                                file_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::filesystem::last_write_time(req.filepath).time_since_epoch()).count();
                            } catch (...) {}
                        }
                        meta.file_size = file_size;
                        meta.file_timestamp = file_timestamp;
                        meta.orig_width = true_w;
                        meta.orig_height = true_h;
                        db_.store_image_metadata(hash, meta);
                    }

                    cached = transaction.commit();
                } else {
                    db_.abort_transaction();
                }
            }
            if (!cached) {
                update_queue_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::DATABASE_ERROR,
                    "Could not save thumbnails. Check free disk space and cache permissions, then rescan."));
            }
        }

        update_queue_.enqueue(UpdateEvent::make_thumb_rgb_ready(
            req.image_index, req.filepath, hash, req.target_quality, std::move(resized), target_w, target_h, req.generation
        ), req.token);
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

bool ThumbnailPipeline::load_jpeg_scaled_file(const std::string& path, int tw, int th,
        std::vector<uint8_t>& rgb, int& w, int& h, int* ow, int* oh) {
    return load_jpeg_scaled(path, tw, th, rgb, w, h, ow, oh);
}

bool ThumbnailPipeline::generate_png_streaming(size_t, const std::string& filepath, int max_w, int max_h,
        std::vector<uint8_t>& out_rgb, int& out_w, int& out_h, int* orig_w, int* orig_h, JobToken) {
    return load_png_file(filepath, max_w, max_h, out_rgb, out_w, out_h, orig_w, orig_h, &stop_requested_);
}
