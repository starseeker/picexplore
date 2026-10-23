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
    : update_queue_(update_queue, session, &stop_requested_), db_path_(db_path), stop_requested_(false) {
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
    if (!workers_.empty()) return;
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
    std::lock_guard<std::mutex> lock(wake_mutex_);
    for (auto it = requests_.begin(); it != requests_.end();) {
        if (it->second.generation && it->second.generation < gen) it = requests_.erase(it);
        else ++it;
    }
}
uint64_t ThumbnailPipeline::get_generation() const { return current_generation_; }

bool ThumbnailPipeline::request_thumbnail(size_t id, const std::string& path, const std::string& hash,
        ThumbQuality quality, bool urgent, uint64_t generation, int w, int h, uint64_t revision) {
    std::lock_guard<std::mutex> lock(wake_mutex_);
    const auto key = std::make_pair(id, urgent);
    if (stop_requested_ || (requests_.size() >= MAX_REQUESTS && !requests_.count(key))) return false;
    requests_[key] = {id, path, quality, hash, generation, w, h, {update_queue_.session(), id, revision, 0}};
    wake_cv_.notify_one();
    return true;
}
void ThumbnailPipeline::worker_thread() {
    while (!stop_requested_) {
        ThumbRequest req;
        bool urgent = false;
        {
            std::unique_lock<std::mutex> lock(wake_mutex_);
            wake_cv_.wait(lock, [this] { return stop_requested_ || !requests_.empty(); });
            if (stop_requested_) break;
            auto next = std::find_if(requests_.begin(), requests_.end(), [](const auto& item) { return item.first.second; });
            if (next == requests_.end()) next = requests_.begin();
            urgent = next->first.second;
            req = std::move(next->second);
            requests_.erase(next);
        }
        if (req.generation && req.generation < current_generation_) continue;
        WorkPermit work(&stop_requested_, urgent);
        if (!work) break;
        process_request(req);
    }
}

bool ThumbnailPipeline::process_request(const ThumbRequest& req, bool is_upgrade) {
    try {
        FileStamp before, after;
        if (!read_file_stamp(req.filepath, before)) return false;
        std::string hash;
        if (!db_.is_open() || !db_.get_valid_hash_for_path(req.filepath, hash)) {
            if (!compute_file_hash(req.filepath, hash, &stop_requested_)) return false;
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
            int cache_w = 0, cache_h = 0, cache_channels = 0;
            if (!stbi_info_from_memory(jpeg_data.data(), jpeg_data.size(), &cache_w, &cache_h, &cache_channels) ||
                cache_w <= 0 || cache_h <= 0 || uint64_t(cache_w) * cache_h > 16000000) target_found = false;
        }
        if (target_found && !is_upgrade) {
            auto memory = ByteBudget::decode().acquire(64ULL * 1024 * 1024, &stop_requested_);
            if (!memory) return false;
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

                // The fast placeholder is visible while this bounded worker upgrades it.
                if (req.generation && req.generation < current_generation_) return true;
                memory.reset();
                return process_request(req, true);
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

        target_h = std::clamp(target_h, 1, 4096);
        target_w = std::clamp(target_w, 1, 4096);

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
    } catch (const std::exception& e) {
        auto event = UpdateEvent::make_thumb_failed(req.image_index, req.filepath, req.target_quality);
        event.message = e.what();
        update_queue_.enqueue(std::move(event), req.token);
        return false;
    }
}

bool ThumbnailPipeline::decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height) {
    return decode_jpeg_memory(jpeg_data, jpeg_size, rgb_data, width, height, true);
}
