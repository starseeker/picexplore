#include "thumbnail_pipeline.h"
#include <jpeglib.h>
#include <setjmp.h>
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
                    req.image_index, req.filepath, req.target_quality, rgb_out, target_w, target_h
                ));
                return true;
            }
        }

        int w, h, channels;
        unsigned char* img = stbi_load(req.filepath.c_str(), &w, &h, &channels, 3);
        if (!img) return false;

        if (hash.empty()) {
            XXH64_hash_t hval = XXH64(img, w * h * 3, 0);
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

        std::vector<uint8_t> resized(target_w * target_h * 3);
        stbir_resize_uint8_linear(img, w, h, 0, resized.data(), target_w, target_h, 0, STBIR_RGB);
        stbi_image_free(img);

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
            req.image_index, req.filepath, req.target_quality, resized, target_w, target_h
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
