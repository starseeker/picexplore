#include "image_store.h"
#include <jpeglib.h>
#include <setjmp.h>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include "../third_party/stb/stb_image_resize2.h"
#include <iostream>

ImageStore::ImageStore() {
}

ImageStore::~ImageStore() {
}

size_t ImageStore::add_image(const std::string& filepath, double aspect_ratio,
                             int width, int height,
                             uintmax_t file_size, uintmax_t file_timestamp) {
    size_t idx = entries_.size();
    ImageEntry entry;
    entry.filepath = filepath;
    entry.index = idx;
    entry.aspect_ratio = aspect_ratio;
    entry.original_width = width;
    entry.original_height = height;
    entry.file_size = file_size;
    entry.file_timestamp = file_timestamp;
    if (width > 0 && height > 0) {
        entry.metadata_known = true;
    }
    entries_.push_back(entry);
    return idx;
}

// Custom error handler for libjpeg
struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void my_error_exit(j_common_ptr cinfo) {
    my_error_mgr* myerr = (my_error_mgr*)cinfo->err;
    longjmp(myerr->setjmp_buffer, 1);
}

bool ImageStore::decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height) {
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
    jpeg_mem_src(&cinfo, jpeg_data, jpeg_size);
    jpeg_read_header(&cinfo, TRUE);
    
    // We want RGB output
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

void ImageStore::remove_image(const std::string& filepath) {
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
    
    entries_.erase(entries_.begin() + index);
    
    std::unordered_map<size_t, std::list<size_t>::iterator> new_lru_map;
    for (auto list_it = lru_list_.begin(); list_it != lru_list_.end(); ) {
        if (*list_it == index) {
            list_it = lru_list_.erase(list_it);
        } else {
            if (*list_it > index) {
                *list_it = *list_it - 1;
            }
            new_lru_map[*list_it] = list_it;
            ++list_it;
        }
    }
    lru_map_ = std::move(new_lru_map);
    
    std::vector<size_t> new_visible;
    for (size_t vis_idx : currently_visible_) {
        if (vis_idx == index) continue;
        if (vis_idx > index) new_visible.push_back(vis_idx - 1);
        else new_visible.push_back(vis_idx);
    }
    currently_visible_ = std::move(new_visible);
    
    for (size_t i = index; i < entries_.size(); ++i) {
        entries_[i].index = i;
    }
}

void ImageStore::rename_image(const std::string& old_filepath, const std::string& new_filepath) {
    size_t index = find_by_filepath(old_filepath);
    if (index == static_cast<size_t>(-1)) return;
    entries_[index].filepath = new_filepath;
}


void ImageStore::set_thumbnail(size_t index, const std::string& filepath, ThumbQuality quality,
                               const uint8_t* jpeg_data, size_t jpeg_size,
                               int width, int height) {
    if (index >= entries_.size()) return;
    
    // Verify the file path to handle race conditions during sorting
    if (entries_[index].filepath != filepath) {
        index = find_by_filepath(filepath);
        if (index == static_cast<size_t>(-1)) return;
    }
    
    auto& entry = entries_[index];
    
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
    if (index >= entries_.size()) return;
    
    if (entries_[index].filepath != filepath) {
        index = find_by_filepath(filepath);
        if (index == static_cast<size_t>(-1)) return;
    }
    
    auto& entry = entries_[index];
    
    // If the UI requested a thumbnail with a specific generation, but this incoming
    // thumbnail is from a generic generation=0 background task, only reject it if
    // we ALREADY have a thumbnail of equal or higher quality.
    if (generation == 0 && entry.last_requested_generation > 0 && !entry.scaled.rgb_data.empty() && quality <= entry.scaled.quality) {
        return;
    }
    
    // If this is an older generation UI request arriving out of order, discard it!
    if (generation > 0 && generation < entry.last_fulfilled_generation) return;
    if (generation > 0) {
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
    update_lru(index);
    return entries_[index];
}

const ImageEntry& ImageStore::get(size_t index) const {
    return entries_[index];
}

const uint8_t* ImageStore::get_scaled_image(size_t index, int draw_w, int draw_h) {
    auto& entry = get(index); // updates LRU
    
    if (entry.scaled.rgb_data.empty()) {
        return nullptr;
    }

    if (entry.scaled.layout_width != draw_w || entry.scaled.layout_height != draw_h) {
        return nullptr;
    }
    
    return entry.scaled.rgb_data.data();
}

size_t ImageStore::count() const {
    return entries_.size();
}

std::vector<double> ImageStore::get_aspect_ratios() const {
    std::vector<double> ratios(entries_.size());
    for (size_t i = 0; i < entries_.size(); ++i) {
        ratios[i] = entries_[i].aspect_ratio;
    }
    return ratios;
}

std::vector<std::pair<size_t,double>> ImageStore::get_filtered_aspects(const std::string& dir) const {
    std::vector<std::pair<size_t,double>> result;
    result.reserve(entries_.size());

    if (dir.empty()) {
        for (size_t i = 0; i < entries_.size(); ++i) {
            result.emplace_back(i, entries_[i].aspect_ratio);
        }
    } else {
        // Match any filepath that lives directly in or under `dir`.
        // We use filesystem::path to avoid substring false-positives like
        // filter="/foo/bar" accidentally matching "/foo/bard/img.jpg".
        std::string prefix = dir;
        if (prefix.back() != '/') prefix += '/';
        for (size_t i = 0; i < entries_.size(); ++i) {
            const std::string& fp = entries_[i].filepath;
            if (fp.size() > prefix.size() && fp.compare(0, prefix.size(), prefix) == 0) {
                result.emplace_back(i, entries_[i].aspect_ratio);
            }
        }
    }
    return result;
}


void ImageStore::sort_entries(SortCriteria criteria, bool ascending) {
    if (criteria == SortCriteria::FILE_SIZE || criteria == SortCriteria::TIMESTAMP) {
        for (auto& entry : entries_) {
            if (entry.file_size == 0 || entry.file_timestamp == 0) {
                try {
                    entry.file_size = std::filesystem::file_size(entry.filepath);
                    entry.file_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                        std::filesystem::last_write_time(entry.filepath).time_since_epoch()).count();
                } catch (...) {}
            }
        }
    }

    std::sort(entries_.begin(), entries_.end(), [criteria, ascending](const ImageEntry& a, const ImageEntry& b) {
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
        }
        return a.filepath < b.filepath;
    });

    // Reassign indices
    for (size_t i = 0; i < entries_.size(); ++i) {
        entries_[i].index = i;
    }
    
    // Clear visibility state because indices changed completely
    currently_visible_.clear();
    
    // We cannot just clear lru_list_ because the entries still hold memory.
    // If we clear lru_list_, the eviction engine cannot free the memory of sorted images,
    // leading to an infinite eviction loop for new images.
    lru_list_.clear();
    lru_map_.clear();
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].decoded.rgb_data.empty() || !entries_[i].scaled.rgb_data.empty()) {
            lru_list_.push_back(i);
            lru_map_[i] = std::prev(lru_list_.end());
        }
    }
}

size_t ImageStore::find_by_filepath(const std::string& filepath) const {
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].filepath == filepath) return i;
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
    currently_visible_ = visible_indices;
    // Mark these as recently used
    for (size_t idx : visible_indices) {
        update_lru(idx);
    }
}

void ImageStore::evict_memory_if_needed() {
    decoded_rgb_memory_used_ = 0;
    scaled_rgb_memory_used_ = 0;
    for (const auto& entry : entries_) {
        decoded_rgb_memory_used_ += entry.decoded.rgb_data.size();
        scaled_rgb_memory_used_ += entry.scaled.rgb_data.size();
    }

    while ((decoded_rgb_memory_used_ > MAX_DECODED_MEMORY || 
            scaled_rgb_memory_used_ > MAX_SCALED_MEMORY) && !lru_list_.empty()) {
        
        // Find least recently used that is NOT visible
        bool found = false;
        for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
            size_t idx = *it;
            if (std::find(currently_visible_.begin(), currently_visible_.end(), idx) == currently_visible_.end()) {
                // Evict this one
                auto& entry = entries_[idx];
                decoded_rgb_memory_used_ -= entry.decoded.rgb_data.size();
                scaled_rgb_memory_used_ -= entry.scaled.rgb_data.size();
                
                entry.decoded.rgb_data.clear();
                entry.decoded.rgb_data.shrink_to_fit();
                entry.decoded.width = 0;
                entry.decoded.height = 0;
                entry.decoded.quality = ThumbQuality::NONE;
                entry.best_quality = ThumbQuality::NONE; 
                
                entry.scaled.rgb_data.clear();
                entry.scaled.rgb_data.shrink_to_fit();
                entry.scaled.width = 0;
                entry.scaled.height = 0;
                entry.scaled.layout_width = 0;
                entry.scaled.layout_height = 0;
                entry.scaled.quality = ThumbQuality::NONE;
                
                auto map_it = lru_map_.find(idx);
                lru_list_.erase(map_it->second);
                lru_map_.erase(map_it);
                
                found = true;
                break;
            }
        }
        if (!found) break; // Everything is visible, can't evict
    }
}
