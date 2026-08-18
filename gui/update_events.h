#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Thumbnail quality levels
enum class ThumbQuality {
    FAILED = -1,   // Failed to generate
    NONE = 0,      // Grey rectangle only
    TINY = 32,     // 32px thumbnail
    SMALL = 64,    // 64px thumbnail
    MEDIUM = 128,  // 128px thumbnail
    LARGE = 256,   // 256px thumbnail
    XLARGE = 512,  // 512px thumbnail
    FULL = 1024    // 1024px thumbnail
};

struct UpdateEvent {
    enum class Type {
        IMAGE_DISCOVERED,
        IMAGE_DELETED,
        IMAGE_RENAMED,
        THUMB_READY,
        THUMB_RGB_READY,
        THUMB_FAILED,
        FULL_RES_READY,
        SCAN_PROGRESS,
        SCAN_COMPLETE,
        TILE_GENERATION_PROGRESS
    };

    Type type;

    // Payload for IMAGE_DISCOVERED
    struct {
        std::string filepath;
        std::string content_hash;
        int width;
        int height;
        double aspect_ratio;
        uintmax_t file_size;
        uintmax_t file_timestamp;
        ThumbQuality best_quality;
        std::vector<uint8_t> jpeg_data;
        int thumb_width;
        int thumb_height;
    } image;

    // Payload for THUMB_READY
    struct {
        size_t image_index;
        std::string filepath;
        ThumbQuality quality;
        std::vector<uint8_t> jpeg_data;
        int width;
        int height;
    } thumb;

    // Payload for THUMB_RGB_READY
    struct {
        size_t image_index;
        std::string filepath;
        ThumbQuality quality;
        std::vector<uint8_t> rgb_data;
        int width;
        int height;
        uint64_t generation;
    } thumb_rgb;

    // Payload for FULL_RES_READY
    struct {
        size_t image_index;
        std::string filepath;
        std::vector<uint8_t> rgb_data;
        int width;
        int height;
    } full_res;

    // Payload for SCAN_PROGRESS
    struct {
        int found;
    } progress;

    // Payload for IMAGE_DELETED
    struct {
        std::string filepath;
    } deletion;

    // Payload for IMAGE_RENAMED
    struct {
        std::string old_filepath;
        std::string new_filepath;
    } rename;

    // Payload for THUMB_FAILED
    struct {
        size_t image_index;
        std::string filepath;
        ThumbQuality target_quality;
    } failed;

    // Payload for TILE_GENERATION_PROGRESS
    struct {
        size_t image_index;
        std::string filepath;
        int current_row;
        int total_rows;
    } tile_progress;

    // Helper constructors
    static UpdateEvent make_tile_progress(size_t index, const std::string& filepath, int current, int total) {
        UpdateEvent ev;
        ev.type = Type::TILE_GENERATION_PROGRESS;
        ev.tile_progress.image_index = index;
        ev.tile_progress.filepath = filepath;
        ev.tile_progress.current_row = current;
        ev.tile_progress.total_rows = total;
        return ev;
    }

    static UpdateEvent make_image_discovered(
        const std::string& path, const std::string& hash,
        int w, int h, double ar, uintmax_t fsize = 0, uintmax_t ftime = 0,
        ThumbQuality bq = ThumbQuality::NONE, const std::vector<uint8_t>& jpeg_data = {},
        int thumb_w = 0, int thumb_h = 0) {
        UpdateEvent ev;
        ev.type = Type::IMAGE_DISCOVERED;
        ev.image.filepath = path;
        ev.image.content_hash = hash;
        ev.image.width = w;
        ev.image.height = h;
        ev.image.aspect_ratio = ar;
        ev.image.file_size = fsize;
        ev.image.file_timestamp = ftime;
        ev.image.best_quality = bq;
        ev.image.jpeg_data = jpeg_data;
        ev.image.thumb_width = thumb_w;
        ev.image.thumb_height = thumb_h;
        return ev;
    }

    static UpdateEvent make_thumb_failed(size_t index, const std::string& filepath, ThumbQuality q) {
        UpdateEvent ev;
        ev.type = Type::THUMB_FAILED;
        ev.failed.image_index = index;
        ev.failed.filepath = filepath;
        ev.failed.target_quality = q;
        return ev;
    }

    static UpdateEvent make_thumb_ready(
        size_t index, const std::string& filepath, ThumbQuality q,
        const std::vector<uint8_t>& jpeg_data, int w, int h) {
        UpdateEvent ev;
        ev.type = Type::THUMB_READY;
        ev.thumb.image_index = index;
        ev.thumb.filepath = filepath;
        ev.thumb.quality = q;
        ev.thumb.jpeg_data = jpeg_data;
        ev.thumb.width = w;
        ev.thumb.height = h;
        return ev;
    }

    static UpdateEvent make_thumb_rgb_ready(
        size_t index, const std::string& filepath, ThumbQuality q,
        const std::vector<uint8_t>& rgb, int w, int h, uint64_t generation) {
        UpdateEvent ev;
        ev.type = Type::THUMB_RGB_READY;
        ev.thumb_rgb.image_index = index;
        ev.thumb_rgb.filepath = filepath;
        ev.thumb_rgb.quality = q;
        ev.thumb_rgb.rgb_data = rgb;
        ev.thumb_rgb.width = w;
        ev.thumb_rgb.height = h;
        ev.thumb_rgb.generation = generation;
        return ev;
    }

    static UpdateEvent make_full_res_ready(
        size_t index, const std::string& filepath,
        const std::vector<uint8_t>& rgb, int w, int h) {
        UpdateEvent ev;
        ev.type = Type::FULL_RES_READY;
        ev.full_res.image_index = index;
        ev.full_res.filepath = filepath;
        ev.full_res.rgb_data = rgb;
        ev.full_res.width = w;
        ev.full_res.height = h;
        return ev;
    }

    static UpdateEvent make_scan_progress(int found) {
        UpdateEvent e;
        e.type = Type::SCAN_PROGRESS;
        e.progress.found = found;
        return e;
    }

    static UpdateEvent make_scan_complete() {
        UpdateEvent e;
        e.type = Type::SCAN_COMPLETE;
        return e;
    }

    static UpdateEvent make_image_deleted(const std::string& path) {
        UpdateEvent e;
        e.type = Type::IMAGE_DELETED;
        e.deletion.filepath = path;
        return e;
    }

    static UpdateEvent make_image_renamed(const std::string& old_path, const std::string& new_path) {
        UpdateEvent e;
        e.type = Type::IMAGE_RENAMED;
        e.rename.old_filepath = old_path;
        e.rename.new_filepath = new_path;
        return e;
    }
};
