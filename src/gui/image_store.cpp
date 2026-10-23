#include "image_store.h"
#include "database.h"
#include "sift_feature.h"
#include "utils.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <thread>
#include <atomic>
#include "../third_party/stb/stb_image_resize2.h"
#include "../third_party/stb/stb_image.h"
#include <iostream>

ImageStore::ImageStore() {
}

ImageStore::~ImageStore() {
}

void ImageStore::clear() {
    entries_.clear();
    order_.clear();
    path_to_index_.clear();
    all_aspects_cache_.clear();
    filtered_aspects_cache_.clear();
    aspects_dirty_ = true;
    currently_visible_set_.clear();
    lru_list_.clear();
    lru_map_.clear();
    decoded_rgb_memory_used_ = 0;
    scaled_rgb_memory_used_ = 0;
}

size_t ImageStore::add_image(std::string filepath, double aspect_ratio,
                             int width, int height, uintmax_t file_size,
                             uintmax_t file_timestamp, std::string hash,
                             FileStamp stamp) {
    size_t idx = find_by_filepath(filepath);
    if (idx == static_cast<size_t>(-1)) {
        idx = next_id_++;
        entries_.emplace(idx, ImageEntry{});
        order_.push_back(idx);
        path_to_index_[filepath] = idx;
    } else {
        auto& old = entries_.at(idx);
        if ((stamp.valid && old.file_stamp != stamp) ||
            (!hash.empty() && old.content_hash != hash)) {
            const auto revision = old.revision + 1;
            decoded_rgb_memory_used_ -= old.decoded.rgb_data.size() + old.square_thumb.rgb_data.size();
            scaled_rgb_memory_used_ -= old.scaled.rgb_data.size();
            old = ImageEntry{};
            old.revision = revision;
        }
    }
    auto& entry = entries_.at(idx);
    entry.filepath = filepath;
    entry.index = idx;
    entry.aspect_ratio = aspect_ratio > 0 ? aspect_ratio : 1;
    entry.original_width = width;
    entry.original_height = height;
    entry.metadata_known = width > 0 && height > 0;
    entry.file_size = file_size;
    entry.file_timestamp = file_timestamp;
    if (stamp.valid) entry.file_stamp = stamp;
    if (!hash.empty()) entry.content_hash = hash;
    aspects_dirty_ = true;
    return idx;
}

bool ImageStore::decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height) {
    return decode_jpeg_memory(jpeg_data, jpeg_size, rgb_data, width, height, false);
}

void ImageStore::remove_image(std::string filepath) {
    size_t index = find_by_filepath(filepath);
    if (index == static_cast<size_t>(-1)) return;
    
    auto& entry = entries_[index];
    decoded_rgb_memory_used_ -= entry.decoded.rgb_data.size();
    scaled_rgb_memory_used_ -= entry.scaled.rgb_data.size();
    
    auto lru_it = lru_map_.find(index);
    if (lru_it != lru_map_.end()) {
        lru_list_.erase(lru_it->second);
        lru_map_.erase(lru_it);
    }
    
    decoded_rgb_memory_used_ -= entry.square_thumb.rgb_data.size();
    entries_.erase(index);
    path_to_index_.erase(filepath);
    order_.erase(std::remove(order_.begin(), order_.end(), index), order_.end());
    currently_visible_set_.erase(index);
    aspects_dirty_ = true;
}

void ImageStore::rename_image(std::string old_filepath, std::string new_filepath) {
    size_t index = find_by_filepath(old_filepath);
    if (index == static_cast<size_t>(-1)) return;
    if (old_filepath == new_filepath) return;
    // rename(2) may replace an existing destination atomically.
    remove_image(new_filepath);
    path_to_index_.erase(old_filepath);
    entries_[index].filepath = new_filepath;
    ++entries_[index].revision;
    entries_[index].last_requested_generation = 0;
    path_to_index_[new_filepath] = index;
    aspects_dirty_ = true;
}


void ImageStore::set_thumbnail(size_t index, const std::string& filepath, ThumbQuality quality,
                               const uint8_t* jpeg_data, size_t jpeg_size,
                               int width, int height) {
    if (!contains(index)) return;
    
    if (entries_[index].filepath != filepath) return;
    
    auto& entry = entries_[index];
    
    if (quality == ThumbQuality::FAILED) { entry.best_quality = quality; return; }

    // THUMB_READY is a generation=0 background task from ScanCoordinator or initial DB load.
    // If the UI has explicitly requested a layout-perfect thumbnail, DO NOT allow this
    // generic background task to overwrite it and clear the scaled layout buffer!
    // That causes permanent grey rectangles!
    if (entry.last_requested_generation > 0) return;
    
    // Only upgrade if it's better or same quality
    if (quality < entry.best_quality) return;

    entry.best_quality = quality;

    // Decode to RGB directly
    std::vector<uint8_t> rgb_data;
    int dec_w, dec_h;
    if (decode_jpeg(jpeg_data, jpeg_size, rgb_data, dec_w, dec_h)) {
        // Remove old memory tracking
        decoded_rgb_memory_used_ -= entry.decoded.rgb_data.size();
        scaled_rgb_memory_used_ -= entry.scaled.rgb_data.size();
        
        entry.decoded.rgb_data = std::move(rgb_data);
        entry.decoded.width = dec_w;
        entry.decoded.height = dec_h;
        entry.decoded.quality = quality;

        // Clear scaled, it needs to be rebuilt
        entry.scaled.rgb_data.clear();
        entry.scaled.layout_width = 0;
        entry.scaled.layout_height = 0;
        
        decoded_rgb_memory_used_ += entry.decoded.rgb_data.size();

        update_lru(index);
        evict_memory_if_needed();
    }
}

void ImageStore::set_thumbnail_rgb(size_t index, const std::string& filepath, ThumbQuality quality,
                                   std::vector<uint8_t>&& rgb_data, int width, int height, uint64_t generation) {
    if (!contains(index)) return;
    
    if (entries_[index].filepath != filepath) return;
    
    auto& entry = entries_[index];
    
    if (quality == ThumbQuality::SQUARE_128 || quality == ThumbQuality::SQUARE_64) {
        decoded_rgb_memory_used_ -= entry.square_thumb.rgb_data.size();
        entry.square_thumb.rgb_data = std::move(rgb_data);
        entry.square_thumb.width = width;
        entry.square_thumb.height = height;
        entry.square_thumb.quality = quality;
        decoded_rgb_memory_used_ += entry.square_thumb.rgb_data.size();
        update_lru(index);
        evict_memory_if_needed();
        return;
    }

    // If the UI requested a thumbnail with a specific generation, but this incoming
    // thumbnail is from a generic generation=0 background task, only reject it if
    // we ALREADY have a thumbnail of equal or higher quality.
    if (generation == 0 && entry.last_requested_generation > 0 && !entry.scaled.rgb_data.empty() && quality <= entry.scaled.quality) {
        return;
    }
    
    // If this is an older generation UI request arriving out of order, only discard if
    // we already have an equal or higher quality image. If this older generation is a
    // sharp upgrade (e.g. completed high-res decode), we should definitely accept it!
    if (generation > 0 && generation < entry.last_fulfilled_generation && !entry.scaled.rgb_data.empty() && quality <= entry.scaled.quality) {
        return;
    }
    if (generation > entry.last_fulfilled_generation) {
        entry.last_fulfilled_generation = generation;
    }
    
    // Only reject if it's a quality downgrade AND the current image is perfectly sized.
    // If the current image is empty, or the layout doesn't match, we MUST accept the new image
    // regardless of quality to avoid drawing a grey square.
    bool layout_matches = (entry.scaled.layout_width > 0 && entry.scaled.layout_width == width);
    if (!entry.scaled.rgb_data.empty() && layout_matches && quality < entry.scaled.quality) {
        return;
    }

    // Update the best quality ONLY if it's actually better.
    if (quality > entry.best_quality) {
        entry.best_quality = quality;
    }

    scaled_rgb_memory_used_ -= entry.scaled.rgb_data.size();
    
    entry.scaled.rgb_data = std::move(rgb_data);
    entry.scaled.width = width;
    entry.scaled.height = height;
    entry.scaled.layout_width = width;
    entry.scaled.layout_height = height;
    entry.scaled.quality = quality;
    
    scaled_rgb_memory_used_ += entry.scaled.rgb_data.size();

    // We no longer store decoded JPEGs
    decoded_rgb_memory_used_ -= entry.decoded.rgb_data.size();
    entry.decoded.rgb_data.clear();
    entry.decoded.rgb_data.shrink_to_fit();
    entry.decoded.width = 0;
    entry.decoded.height = 0;

    evict_memory_if_needed();
    update_lru(index);
}

ImageEntry& ImageStore::get(size_t index) {
    return entries_.at(index);
}

const ImageEntry& ImageStore::get(size_t index) const {
    return entries_.at(index);
}

const uint8_t* ImageStore::get_scaled_image(size_t index, int draw_w, int draw_h) {
    if (!contains(index)) return nullptr;
    auto& entry = entries_[index];
    if (entry.scaled.rgb_data.empty()) {
        return nullptr;
    }
    update_lru(index);
    return entry.scaled.rgb_data.data();
}

size_t ImageStore::count() const {
    return entries_.size();
}

std::vector<double> ImageStore::get_aspect_ratios() const {
    std::vector<double> ratios(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) {
        ratios[i] = at_position(i).aspect_ratio;
    }
    return ratios;
}

const std::vector<std::pair<size_t,double>>& ImageStore::get_filtered_aspects(const std::string& dir) const {
    if (dir.empty()) {
        if (aspects_dirty_) {
            all_aspects_cache_.clear();
            all_aspects_cache_.reserve(entries_.size());
            for (size_t i = 0; i < entries_.size(); ++i) {
                all_aspects_cache_.emplace_back(order_[i], at_position(i).aspect_ratio);
            }
            aspects_dirty_ = false;
        }
        return all_aspects_cache_;
    } else {
        filtered_aspects_cache_.clear();
        filtered_aspects_cache_.reserve(entries_.size());
        std::string prefix = dir;
        if (prefix.back() != '/') prefix += '/';
        for (size_t i = 0; i < entries_.size(); ++i) {
            const std::string& fp = at_position(i).filepath;
            if (fp.size() > prefix.size() && fp.compare(0, prefix.size(), prefix) == 0) {
                filtered_aspects_cache_.emplace_back(order_[i], at_position(i).aspect_ratio);
            }
        }
        return filtered_aspects_cache_;
    }
}


void ImageStore::ensure_duplicate_counts() {
    std::unordered_map<std::string, int> hash_counts;
    for (auto id : order_) {
        const auto& entry = entries_.at(id);
        if (!entry.content_hash.empty()) {
            hash_counts[entry.content_hash]++;
        }
    }
    for (auto id : order_) {
        auto& entry = entries_.at(id);
        if (!entry.content_hash.empty()) {
            auto it = hash_counts.find(entry.content_hash);
            if (it != hash_counts.end()) {
                entry.duplicate_count = it->second;
            } else {
                entry.duplicate_count = 1;
            }
        } else {
            entry.duplicate_count = 1;
        }
    }
}

void ImageStore::sort_entries(SortCriteria criteria, bool ascending) {
    // Metadata arrives from the reconciler; sorting never performs filesystem I/O.
    if (criteria == SortCriteria::DUPLICATE_COUNT) ensure_duplicate_counts();

    std::sort(order_.begin(), order_.end(), [this, criteria, ascending](size_t aid, size_t bid) {
        const auto& a = get(aid);
        const auto& b = get(bid);
        if (criteria == SortCriteria::ALPHABETICAL) {
            return ascending ? (a.filepath < b.filepath) : (a.filepath > b.filepath);
        } else if (criteria == SortCriteria::FILE_SIZE) {
            if (a.file_size != b.file_size) {
                return ascending ? (a.file_size < b.file_size) : (a.file_size > b.file_size);
            }
            return a.filepath < b.filepath;
        } else if (criteria == SortCriteria::TIMESTAMP) {
            if (a.file_timestamp != b.file_timestamp) {
                return ascending ? (a.file_timestamp < b.file_timestamp) : (a.file_timestamp > b.file_timestamp);
            }
            return a.filepath < b.filepath;
        } else if (criteria == SortCriteria::PIXEL_AREA) {
            long long area_a = (long long)a.original_width * a.original_height;
            long long area_b = (long long)b.original_width * b.original_height;
            if (area_a != area_b) {
                return ascending ? (area_a < area_b) : (area_a > area_b);
            }
            return a.filepath < b.filepath;
        } else if (criteria == SortCriteria::DUPLICATE_COUNT) {
            if (a.duplicate_count != b.duplicate_count) {
                return ascending ? (a.duplicate_count < b.duplicate_count) : (a.duplicate_count > b.duplicate_count);
            }
            return a.filepath < b.filepath;
        }
        return a.filepath < b.filepath;
    });

    aspects_dirty_ = true;
}

struct SimilarityResult {
    double score = 0.0;
    std::array<float, 64> global_vec{};
    bool loaded = false;
};

bool ImageStore::sort_by_similarity(size_t query_index, bool high_accuracy, DatabaseManager* db,
                                    std::atomic<size_t>* progress_counter,
                                    int preferred_size,
                                    std::atomic<bool>* stop_requested) {
    if (!contains(query_index)) return false;

    std::string query_path = entries_[query_index].filepath;
    std::string query_hash = entries_[query_index].content_hash;

    // Ensure query has SIFT features
    SiftFeatureData query_sift;
    if (db && !query_hash.empty()) {
        if (!db->get_sift_features(query_hash, query_sift)) {
            db->extract_sift_from_cached_thumbnail(query_hash, query_sift, preferred_size);
        }
    }
    if (!query_sift.valid) {
        if (!entries_[query_index].decoded.rgb_data.empty()) {
            SiftFeatureEngine::extract_from_rgb(entries_[query_index].decoded.rgb_data.data(),
                                                entries_[query_index].decoded.width,
                                                entries_[query_index].decoded.height,
                                                query_sift);
        } else {
            FileStamp current;
            if (entries_[query_index].file_stamp.valid &&
                (!read_file_stamp(query_path, current) || current != entries_[query_index].file_stamp)) return false;
            auto decoded = decode_image(query_path, preferred_size, preferred_size, stop_requested);
            if (decoded) SiftFeatureEngine::extract_from_rgb(decoded.rgb.data(), decoded.width, decoded.height, query_sift);
        }
    }

    if (!query_sift.valid) {
        std::cerr << "Warning: Failed to extract SIFT features for query image: " << query_path << std::endl;
        return false;
    }

    const size_t total = entries_.size();
    std::vector<SimilarityResult> results(total);

    // Query item always has similarity 1.0
    const size_t query_position = std::find(order_.begin(), order_.end(), query_index) - order_.begin();
    results[query_position].score = 1.0;
    results[query_position].global_vec = query_sift.global_vector;
    results[query_position].loaded = true;

    unsigned int num_threads = WorkPermit::limit();
    std::vector<std::thread> workers;
    size_t chunk_size = (total + num_threads - 1) / num_threads;

    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start = t * chunk_size;
        size_t end = std::min(total, start + chunk_size);
        if (start >= end) continue;

        workers.emplace_back([this, start, end, &query_sift, db, progress_counter, query_index, preferred_size, &results, high_accuracy, stop_requested]() {
            for (size_t i = start; i < end; ++i) {
                if (stop_requested && stop_requested->load(std::memory_order_relaxed)) {
                    break;
                }

                if (order_[i] == query_index) {
                    if (progress_counter) progress_counter->fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                WorkPermit work(stop_requested);
                if (!work) break;
                const auto& entry = at_position(i);
                SiftFeatureData entry_sift;
                bool has_sift = false;

                if (db && !entry.content_hash.empty()) {
                    if (db->get_sift_features(entry.content_hash, entry_sift)) {
                        has_sift = true;
                    } else if (db->extract_sift_from_cached_thumbnail(entry.content_hash, entry_sift, preferred_size)) {
                        has_sift = true;
                    }
                }

                if (!has_sift && !entry.decoded.rgb_data.empty()) {
                    if (SiftFeatureEngine::extract_from_rgb(entry.decoded.rgb_data.data(),
                                                           entry.decoded.width,
                                                           entry.decoded.height,
                                                           entry_sift)) {
                        has_sift = true;
                    }
                }

                if (has_sift && entry_sift.valid) {
                    results[i].global_vec = entry_sift.global_vector;
                    results[i].loaded = true;
                    if (high_accuracy) {
                        results[i].score = SiftFeatureEngine::compute_keypoint_similarity(query_sift, entry_sift);
                    } else {
                        results[i].score = SiftFeatureEngine::compute_fast_similarity(query_sift.global_vector, entry_sift.global_vector);
                    }
                } else if (entry.sift_vector_loaded && !high_accuracy) {
                    results[i].score = SiftFeatureEngine::compute_fast_similarity(query_sift.global_vector, entry.sift_global_vector);
                    results[i].global_vec = entry.sift_global_vector;
                    results[i].loaded = true;
                } else {
                    results[i].score = 0.0;
                }

                if (progress_counter) {
                    progress_counter->fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& w : workers) {
        if (w.joinable()) w.join();
    }

    if (stop_requested && stop_requested->load(std::memory_order_relaxed)) {
        return false;
    }

    // Apply computed scores to ImageStore entries
    for (size_t i = 0; i < total; ++i) {
        at_position(i).similarity_score = results[i].score;
        if (results[i].loaded) {
            at_position(i).sift_global_vector = results[i].global_vec;
            at_position(i).sift_vector_loaded = true;
        }
    }

    // Sort descending by similarity score (satisfying strict weak ordering)
    std::sort(order_.begin(), order_.end(), [this, query_path](size_t aid, size_t bid) {
        const auto& a = get(aid);
        const auto& b = get(bid);
        if (a.filepath == b.filepath) return false;
        if (a.filepath == query_path) return true;
        if (b.filepath == query_path) return false;
        if (a.similarity_score != b.similarity_score) {
            return a.similarity_score > b.similarity_score;
        }
        return a.filepath < b.filepath;
    });

    aspects_dirty_ = true;
    return true;
}

size_t ImageStore::find_by_filepath(const std::string& filepath) const {
    auto it = path_to_index_.find(filepath);
    if (it != path_to_index_.end()) {
        return it->second;
    }
    return static_cast<size_t>(-1);
}

void ImageStore::update_lru(size_t index) {
    auto it = lru_map_.find(index);
    if (it != lru_map_.end()) {
        lru_list_.erase(it->second);
    }
    lru_list_.push_front(index);
    lru_map_[index] = lru_list_.begin();
}

void ImageStore::mark_visible(const std::vector<size_t>& visible_indices) {
    currently_visible_set_.clear();
    currently_visible_set_.insert(visible_indices.begin(), visible_indices.end());
}

void ImageStore::evict_memory_if_needed() {
    while ((decoded_rgb_memory_used_ > MAX_DECODED_MEMORY || scaled_rgb_memory_used_ > MAX_SCALED_MEMORY) && !lru_list_.empty()) {
        auto victim = std::prev(lru_list_.end());
        // Prefer off-screen entries, but visibility is not permission to exceed
        // the byte budget (a treemap may expose the entire catalog at once).
        for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
            if (!currently_visible_set_.count(*it)) { victim = std::prev(it.base()); break; }
        }
        const auto id = *victim;
        auto& entry = entries_.at(id);
        decoded_rgb_memory_used_ -= entry.decoded.rgb_data.size() + entry.square_thumb.rgb_data.size();
        scaled_rgb_memory_used_ -= entry.scaled.rgb_data.size();
        entry.decoded = {};
        entry.square_thumb = {};
        entry.scaled = {};
        entry.best_quality = ThumbQuality::NONE;
        lru_map_.erase(id);
        lru_list_.erase(victim);
    }
}

ImageStore ImageStore::snapshot() const {
    ImageStore copy;
    copy.next_id_ = next_id_;
    copy.order_ = order_;
    copy.path_to_index_ = path_to_index_;
    for (auto id : order_) {
        const auto& source = get(id);
        ImageEntry entry;
        entry.index = id;
        entry.revision = source.revision;
        entry.file_stamp = source.file_stamp;
        entry.filepath = source.filepath;
        entry.content_hash = source.content_hash;
        entry.original_width = source.original_width;
        entry.original_height = source.original_height;
        entry.aspect_ratio = source.aspect_ratio;
        entry.metadata_known = source.metadata_known;
        entry.file_size = source.file_size;
        entry.file_timestamp = source.file_timestamp;
        entry.duplicate_count = source.duplicate_count;
        entry.sift_global_vector = source.sift_global_vector;
        entry.sift_vector_loaded = source.sift_vector_loaded;
        entry.similarity_score = source.similarity_score;
        copy.entries_.emplace(id, std::move(entry));
    }
    return copy;
}

bool ImageStore::apply_similarity(const ImageStore& result, size_t query_id) {
    if (!contains(query_id) || !result.contains(query_id) ||
        get(query_id).revision != result.get(query_id).revision) return false;
    for (auto id : order_) {
        auto& entry = get(id);
        entry.similarity_score = 0;
        if (!result.contains(id)) continue;
        const auto& source = result.get(id);
        if (entry.revision != source.revision) continue;
        entry.similarity_score = source.similarity_score;
        entry.sift_global_vector = source.sift_global_vector;
        entry.sift_vector_loaded = source.sift_vector_loaded;
    }
    std::sort(order_.begin(), order_.end(), [this, query_id](size_t a, size_t b) {
        if (a == b) return false;
        if (a == query_id) return true;
        if (b == query_id) return false;
        const auto& left = get(a);
        const auto& right = get(b);
        if (left.similarity_score != right.similarity_score)
            return left.similarity_score > right.similarity_score;
        return left.filepath < right.filepath;
    });
    aspects_dirty_ = true;
    return true;
}
