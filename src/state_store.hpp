/*
 * state_store.hpp - Central state management for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <optional>
#include <shared_mutex>
#include "event_bus.hpp"

// Forward declarations
class DatabaseManager;

/**
 * Basic image information structure for StateStore (independent of database)
 */
struct StateImageInfo {
    std::string path;
    std::string hash;
    double aspect_ratio = 1.0;
    int best_thumb_size = 0;
    std::vector<uint8_t> thumb_data;
    int thumb_width = 0;
    int thumb_height = 0;
    bool has_thumbnails = false;
};

/**
 * Cached thumbnail data structure
 */
struct CachedThumbnail {
    std::vector<uint8_t> data;     // JPEG thumbnail data
    int width = 0;
    int height = 0;
    int size = 0;                  // Size category (32, 64, 128, etc.)
    std::string cache_key;         // "hash:size" format
    std::chrono::steady_clock::time_point last_accessed;
    
    CachedThumbnail() : last_accessed(std::chrono::steady_clock::now()) {}
    CachedThumbnail(const std::vector<uint8_t>& d, int w, int h, int s, const std::string& key)
        : data(d), width(w), height(h), size(s), cache_key(key)
        , last_accessed(std::chrono::steady_clock::now()) {}
};

/**
 * Image metadata with additional state information
 */
struct ImageState {
    StateImageInfo metadata;                    // Core image metadata
    std::vector<int> available_thumbnail_sizes;  // Which thumbnail sizes are available
    bool thumbnails_generated = false;     // Whether all thumbnails have been generated
    bool is_being_processed = false;       // Whether thumbnails are currently being generated
    std::chrono::steady_clock::time_point last_updated;
    
    ImageState() : last_updated(std::chrono::steady_clock::now()) {}
    ImageState(const StateImageInfo& info) : metadata(info), last_updated(std::chrono::steady_clock::now()) {}
};

/**
 * Scan progress state
 */
struct ScanState {
    bool is_active = false;
    int current_count = 0;
    int total_count = 0;
    std::string status_message;
    std::string directory_path;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
};

/**
 * StateStore - Central state management for picexplore
 * 
 * The StateStore serves as the single source of truth for:
 * - Image metadata (paths, hashes, aspect ratios)
 * - Thumbnail availability and cache
 * - Scan progress and status
 * 
 * It provides a thread-safe interface and publishes events through the EventBus
 * when state changes occur, allowing UI and background services to react to updates.
 */
class StateStore {
public:
    StateStore();
    ~StateStore();

    /**
     * Event bus access for subscribing to state changes
     */
    EventBus& get_event_bus() { return event_bus_; }
    const EventBus& get_event_bus() const { return event_bus_; }

    //==========================================================================
    // Image Metadata Management
    //==========================================================================
    
    /**
     * Add or update image metadata
     * Publishes IMAGE_METADATA_ADDED or IMAGE_METADATA_UPDATED events
     */
    void add_image_metadata(const StateImageInfo& info);
    
    /**
     * Get image metadata by hash
     * Returns nullptr if not found
     */
    std::shared_ptr<const ImageState> get_image_state(const std::string& hash) const;
    
    /**
     * Get all image metadata
     * Returns a copy of the current image list
     */
    std::vector<ImageState> get_all_images() const;
    
    /**
     * Get count of images
     */
    size_t get_image_count() const;
    
    /**
     * Check if image exists
     */
    bool has_image(const std::string& hash) const;

    //==========================================================================
    // Thumbnail Management
    //==========================================================================
    
    /**
     * Add thumbnail data to cache
     * Publishes THUMBNAIL_READY or THUMBNAIL_UPDATED events
     */
    void add_thumbnail(const std::string& hash, int size, const std::vector<uint8_t>& data, 
                      int width, int height);
    
    /**
     * Get thumbnail data from cache
     * Returns nullptr if not found
     */
    std::shared_ptr<const CachedThumbnail> get_thumbnail(const std::string& hash, int size) const;
    
    /**
     * Check if thumbnail exists
     */
    bool has_thumbnail(const std::string& hash, int size) const;
    
    /**
     * Mark image as having all thumbnails generated
     */
    void mark_thumbnails_generated(const std::string& hash, const std::vector<int>& sizes);
    
    /**
     * Mark image as being processed for thumbnail generation
     */
    void mark_image_processing(const std::string& hash, bool processing);
    
    /**
     * Get available thumbnail sizes for an image
     */
    std::vector<int> get_available_thumbnail_sizes(const std::string& hash) const;

    //==========================================================================
    // Scan State Management
    //==========================================================================
    
    /**
     * Start scan state tracking
     * Publishes SCAN_STARTED event
     */
    void start_scan(const std::string& directory_path);
    
    /**
     * Update scan progress
     * Publishes SCAN_PROGRESS event
     */
    void update_scan_progress(int current, int total, const std::string& status);
    
    /**
     * Complete scan
     * Publishes SCAN_COMPLETED event
     */
    void complete_scan();
    
    /**
     * Cancel scan
     * Publishes SCAN_CANCELLED event
     */
    void cancel_scan();
    
    /**
     * Get current scan state
     */
    ScanState get_scan_state() const;

    //==========================================================================
    // Cache Management
    //==========================================================================
    
    /**
     * Clear thumbnail cache
     */
    void clear_thumbnail_cache();
    
    /**
     * Get cache statistics
     */
    struct CacheStats {
        size_t image_count = 0;
        size_t thumbnail_count = 0;
        size_t cache_memory_usage = 0; // Approximate bytes
    };
    CacheStats get_cache_stats() const;

private:
    // Thread safety
    mutable std::shared_mutex state_mutex_;  // Allows multiple readers, single writer
    
    // Event system
    EventBus event_bus_;
    
    // State storage
    std::unordered_map<std::string, std::shared_ptr<ImageState>> images_by_hash_;
    std::unordered_map<std::string, std::shared_ptr<CachedThumbnail>> thumbnail_cache_;
    ScanState scan_state_;
    
    // Helper methods
    void publish_image_event(StateEventType type, const ImageState& state);
    void publish_thumbnail_event(StateEventType type, const std::string& hash, 
                                const CachedThumbnail& thumbnail);
    void publish_scan_event(StateEventType type);
    std::string make_thumbnail_cache_key(const std::string& hash, int size) const;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s