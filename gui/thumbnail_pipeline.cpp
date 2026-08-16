#include "thumbnail_pipeline.h"
#include "../third_party/stb/stb_image.h"
#include "../third_party/stb/stb_image_resize2.h"
#include <jpeglib.h>
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

void ThumbnailPipeline::request_thumbnail(size_t image_index, const std::string& filepath, const std::string& hash, ThumbQuality quality, bool urgent) {
    ThumbRequest req{image_index, filepath, quality, hash};
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
    if (!req.hash.empty() && db_.is_open()) {
        std::lock_guard<std::mutex> lock(db_.get_mutex());
        if (db_.begin_transaction()) {
            std::string key = req.hash + ":" + std::to_string(static_cast<int>(req.target_quality));
            std::vector<uint8_t> data;
            bool found = db_.get_key_data(key, data);
            db_.abort_transaction();
            if (found) {
                update_queue_.enqueue(UpdateEvent::make_thumb_ready(
                    req.image_index, req.target_quality, data, 0, 0
                ));
                return true;
            }
        }
    }

    int w, h, channels;
    unsigned char* img = stbi_load(req.filepath.c_str(), &w, &h, &channels, 3);
    if (!img) return false;

    std::string hash = req.hash;
    if (hash.empty()) {
        XXH64_hash_t hval = XXH64(img, w * h * 3, 0);
        char hash_str[17];
        snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hval);
        hash = hash_str;
    }

    int tw = static_cast<int>(req.target_quality);
    double ar = static_cast<double>(w) / h;
    int target_w, target_h;
    if (w > h) {
        target_w = tw;
        target_h = static_cast<int>(tw / ar);
    } else {
        target_h = tw;
        target_w = static_cast<int>(tw * ar);
    }

    target_h = std::max(1, target_h);
    target_w = std::max(1, target_w);

    std::vector<uint8_t> resized(target_w * target_h * 3);
    stbir_resize_uint8_linear(img, w, h, 0, resized.data(), target_w, target_h, 0, STBIR_RGB);
    stbi_image_free(img);

    std::vector<uint8_t> jpeg_data = encode_jpeg(resized.data(), target_w, target_h, 90);

    if (db_.is_open() && !hash.empty()) {
        std::lock_guard<std::mutex> lock(db_.get_mutex());
        if (db_.begin_transaction()) {
            std::string key = hash + ":" + std::to_string(static_cast<int>(req.target_quality));
            if (db_.store_key_data(key, jpeg_data)) {
                db_.commit_transaction();
            } else {
                db_.abort_transaction();
            }
        }
    }

    update_queue_.enqueue(UpdateEvent::make_thumb_ready(
        req.image_index, req.target_quality, jpeg_data, target_w, target_h
    ));

    return true;
}
