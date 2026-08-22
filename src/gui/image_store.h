#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "update_events.h"

struct ImageEntry {
    // Identity
    std::string filepath;
    std::string content_hash;
    size_t index;

    // Metadata
    int original_width = 0;
    int original_height = 0;
    double aspect_ratio = 1.0;
    bool metadata_known = false;
    uintmax_t file_size = 0;
    uintmax_t file_timestamp = 0;
    int duplicate_count = 1;

    // Highest quality we have available
    ThumbQuality best_quality = ThumbQuality::NONE;
    
    // Request tracking to prevent queue spam
    uint64_t last_requested_generation = 0;
    uint64_t last_fulfilled_generation = 0;

    // Decoded RGB cache
    struct DecodedThumb {
        std::vector<uint8_t> rgb_data;
        int width = 0;
        int height = 0;
        ThumbQuality quality = ThumbQuality::NONE;
    };
    DecodedThumb decoded;
    DecodedThumb square_thumb;

    // Pre-scaled image for current layout dimensions
    struct ScaledImage {
        std::vector<uint8_t> rgb_data;
        int width = 0;
        int height = 0;
        int layout_width = 0;
        int layout_height = 0;
        ThumbQuality quality = ThumbQuality::NONE;
    };
    ScaledImage scaled;
};

class ImageStore {
public:
    ImageStore();
    ~ImageStore();

    // Clear all entries and caches
    void clear();

    // Add a new image (from scan discovery or LMDB load)
    size_t add_image(const std::string& filepath, double aspect_ratio,
                     int width = 0, int height = 0,
                     uintmax_t file_size = 0, uintmax_t file_timestamp = 0);

    // Remove an image (e.g. from file deletion)
    void remove_image(const std::string& filepath);

    // Rename an image (e.g. from file rename)
    void rename_image(const std::string& old_filepath, const std::string& new_filepath);

    void set_thumbnail(size_t index, const std::string& filepath, ThumbQuality quality,
                       const uint8_t* jpeg_data, size_t jpeg_size,
                       int width, int height);

    void set_thumbnail_rgb(size_t index, const std::string& filepath, ThumbQuality quality,
                           std::vector<uint8_t>&& rgb_data, int width, int height, uint64_t generation);

    // Access
    ImageEntry& get(size_t index);
    const ImageEntry& get(size_t index) const;
    const uint8_t* get_scaled_image(size_t index, int target_w, int target_h);
    size_t count() const;
    size_t size() const { return count(); }

    // Aspect ratio list for layout engine
    std::vector<double> get_aspect_ratios() const;

    // Filtered aspect ratios for layout engine.
    // Returns {raw_store_index, aspect_ratio} pairs.
    // If dir is empty, returns all images (equivalent to get_aspect_ratios with indices).
    const std::vector<std::pair<size_t,double>>& get_filtered_aspects(const std::string& dir) const;

    enum class SortCriteria {
        ALPHABETICAL,
        FILE_SIZE,
        TIMESTAMP,
        PIXEL_AREA,
        DUPLICATE_COUNT
    };
    void sort_entries(SortCriteria criteria, bool ascending);
    void ensure_file_sizes();
    void ensure_pixel_dimensions();
    void ensure_duplicate_counts();
    size_t find_by_filepath(const std::string& filepath) const;

    // Memory management: Evict unused RGB and scaled data
    void evict_memory_if_needed();
    void mark_visible(const std::vector<size_t>& visible_indices);

private:
    std::vector<ImageEntry> entries_;
    std::unordered_map<std::string, size_t> path_to_index_;

    mutable std::vector<std::pair<size_t, double>> all_aspects_cache_;
    mutable std::vector<std::pair<size_t, double>> filtered_aspects_cache_;
    mutable bool aspects_dirty_ = true;

    // To manage memory budget, we track LRU of DecodedThumb and ScaledImage
    std::unordered_set<size_t> currently_visible_set_;
    
    // Configurable memory budgets (in bytes)
    size_t max_memory_bytes_;
    size_t decoded_rgb_memory_used_ = 0;
    size_t scaled_rgb_memory_used_ = 0;

    size_t MAX_DECODED_MEMORY = 512 * 1024 * 1024; // 512MB
    size_t MAX_SCALED_MEMORY = 256 * 1024 * 1024;  // 256MB

    std::list<size_t> lru_list_;
    std::unordered_map<size_t, std::list<size_t>::iterator> lru_map_;

    void update_lru(size_t index);
    bool decode_jpeg(const uint8_t* jpeg_data, size_t jpeg_size, std::vector<uint8_t>& rgb_data, int& width, int& height);
};
