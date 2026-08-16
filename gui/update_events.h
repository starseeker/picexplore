#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Thumbnail quality levels
enum class ThumbQuality {
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
        THUMB_READY,
        SCAN_PROGRESS,
        SCAN_COMPLETE
    };

    Type type;

    // Payload for IMAGE_DISCOVERED
    struct {
        std::string filepath;
        std::string content_hash;
        int width;
        int height;
        double aspect_ratio;
        ThumbQuality best_quality;
        std::vector<uint8_t> jpeg_data;
        int thumb_width;
        int thumb_height;
    } image;

    // Payload for THUMB_READY
    struct {
        size_t image_index;
        ThumbQuality quality;
        std::vector<uint8_t> jpeg_data;
        int width;
        int height;
    } thumb;

    // Payload for SCAN_PROGRESS
    struct {
        int found;
    } progress;

    // Helper constructors
    static UpdateEvent make_image_discovered(
        const std::string& path, const std::string& hash,
        int w, int h, double ar,
        ThumbQuality bq = ThumbQuality::NONE,
        const std::vector<uint8_t>& jpeg = {},
        int tw = 0, int th = 0) 
    {
        UpdateEvent e;
        e.type = Type::IMAGE_DISCOVERED;
        e.image.filepath = path;
        e.image.content_hash = hash;
        e.image.width = w;
        e.image.height = h;
        e.image.aspect_ratio = ar;
        e.image.best_quality = bq;
        e.image.jpeg_data = jpeg;
        e.image.thumb_width = tw;
        e.image.thumb_height = th;
        return e;
    }

    static UpdateEvent make_thumb_ready(
        size_t idx, ThumbQuality q,
        const std::vector<uint8_t>& jpeg, int w, int h)
    {
        UpdateEvent e;
        e.type = Type::THUMB_READY;
        e.thumb.image_index = idx;
        e.thumb.quality = q;
        e.thumb.jpeg_data = jpeg;
        e.thumb.width = w;
        e.thumb.height = h;
        return e;
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
};
