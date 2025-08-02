/*
 * state_store.cpp - Central state management implementation for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "state_store.hpp"
#include <algorithm>
#include <shared_mutex>

StateStore::StateStore() 
    : thumbnail_cache_(50 * 1024 * 1024, 1000) // Default: 50MB, 1000 items max
{
    setup_cache_eviction_callback();
}

StateStore::StateStore(const CacheConfig& cache_config)
    : thumbnail_cache_(cache_config.max_memory_bytes(), cache_config.max_items)
{
    setup_cache_eviction_callback();
}

StateStore::~StateStore() {
    // Clear all state and ensure event bus is cleaned up
    clear_thumbnail_cache();
    event_bus_.clear_all_subscriptions();
}

//==============================================================================
// Image Metadata Management
//==============================================================================

void StateStore::add_image_metadata(const StateImageInfo& info) {
    bool is_new_image = false;
    StateEventType event_type = StateEventType::IMAGE_METADATA_UPDATED;
    
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        
        auto it = images_by_hash_.find(info.hash);
        if (it == images_by_hash_.end()) {
            // New image
            auto image_state = std::make_shared<ImageState>(info);
            images_by_hash_[info.hash] = image_state;
            is_new_image = true;
            event_type = StateEventType::IMAGE_METADATA_ADDED;
        } else {
            // Update existing image
            it->second->metadata = info;
            it->second->last_updated = std::chrono::steady_clock::now();
        }
    }
    
    // Publish event outside of lock to avoid potential deadlocks
    ImageEvent image_event(event_type, info.hash, info.path, info.aspect_ratio);
    event_bus_.publish(image_event);
}

std::shared_ptr<const ImageState> StateStore::get_image_state(const std::string& hash) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    auto it = images_by_hash_.find(hash);
    if (it != images_by_hash_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<ImageState> StateStore::get_all_images() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    std::vector<ImageState> result;
    result.reserve(images_by_hash_.size());
    
    for (const auto& pair : images_by_hash_) {
        result.push_back(*pair.second);
    }
    
    return result;
}

size_t StateStore::get_image_count() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return images_by_hash_.size();
}

bool StateStore::has_image(const std::string& hash) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return images_by_hash_.find(hash) != images_by_hash_.end();
}

void StateStore::remove_image(const std::string& hash) {
    std::vector<int> removed_thumbnail_sizes;
    std::string removed_path;
    
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        
        // Find the image
        auto img_it = images_by_hash_.find(hash);
        if (img_it == images_by_hash_.end()) {
            return; // Image not found
        }
        
        // Get info for events
        removed_path = img_it->second->metadata.path;
        removed_thumbnail_sizes = img_it->second->available_thumbnail_sizes;
        
        // Remove all thumbnails for this image
        thumbnail_cache_.remove_if([&hash](const std::string& key, const ThumbnailCacheData& data) {
            // Key format is "hash:size", so check if it starts with hash + ":"
            std::string prefix = hash + ":";
            return key.substr(0, prefix.length()) == prefix;
        });
        
        // Remove image metadata
        images_by_hash_.erase(img_it);
    }
    
    // Publish events outside of lock
    ImageEvent image_event(StateEventType::IMAGE_REMOVED, hash, removed_path);
    event_bus_.publish(image_event);
    
    // Publish thumbnail invalidation events
    for (int size : removed_thumbnail_sizes) {
        ThumbnailEvent thumb_event(StateEventType::THUMBNAIL_INVALIDATED, hash, 0, 0, size);
        event_bus_.publish(thumb_event);
    }
}

//==============================================================================
// Thumbnail Management
//==============================================================================

void StateStore::add_thumbnail(const std::string& hash, int size, const std::vector<uint8_t>& data,
                              int width, int height) {
    std::string cache_key = make_thumbnail_cache_key(hash, size);
    bool is_new_thumbnail = false;
    StateEventType event_type = StateEventType::THUMBNAIL_UPDATED;
    
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        
        // Check if thumbnail already exists
        if (!thumbnail_cache_.contains(cache_key)) {
            is_new_thumbnail = true;
            event_type = StateEventType::THUMBNAIL_READY;
        }
        
        // Create thumbnail cache data
        ThumbnailCacheData cache_data(data, width, height, size);
        
        // Add to cache with data size
        thumbnail_cache_.put(cache_key, std::move(cache_data), data.size());
        
        // Update image state to track available thumbnail sizes
        auto img_it = images_by_hash_.find(hash);
        if (img_it != images_by_hash_.end()) {
            auto& sizes = img_it->second->available_thumbnail_sizes;
            if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
                sizes.push_back(size);
                std::sort(sizes.begin(), sizes.end());
            }
            img_it->second->last_updated = std::chrono::steady_clock::now();
        }
    }
    
    // Create a CachedThumbnail for the event (compatibility)
    CachedThumbnail thumbnail(data, width, height, size, cache_key);
    
    // Publish event outside of lock
    ThumbnailEvent thumbnail_event(event_type, hash, width, height, size);
    event_bus_.publish(thumbnail_event);
}

std::shared_ptr<const CachedThumbnail> StateStore::get_thumbnail(const std::string& hash, int size) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    std::string cache_key = make_thumbnail_cache_key(hash, size);
    auto cache_item = thumbnail_cache_.get(cache_key);
    
    if (cache_item) {
        // Convert from cache format to legacy format for compatibility
        auto cached_thumbnail = std::make_shared<CachedThumbnail>(
            cache_item->data.jpeg_data,
            cache_item->data.width,
            cache_item->data.height,
            cache_item->data.size_category,
            cache_key
        );
        return cached_thumbnail;
    }
    
    return nullptr;
}

bool StateStore::has_thumbnail(const std::string& hash, int size) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    std::string cache_key = make_thumbnail_cache_key(hash, size);
    return thumbnail_cache_.contains(cache_key);
}

void StateStore::mark_thumbnails_generated(const std::string& hash, const std::vector<int>& sizes) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    
    auto it = images_by_hash_.find(hash);
    if (it != images_by_hash_.end()) {
        it->second->available_thumbnail_sizes = sizes;
        it->second->thumbnails_generated = true;
        it->second->is_being_processed = false;
        it->second->last_updated = std::chrono::steady_clock::now();
    }
}

void StateStore::mark_image_processing(const std::string& hash, bool processing) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    
    auto it = images_by_hash_.find(hash);
    if (it != images_by_hash_.end()) {
        it->second->is_being_processed = processing;
        it->second->last_updated = std::chrono::steady_clock::now();
    }
}

std::vector<int> StateStore::get_available_thumbnail_sizes(const std::string& hash) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    auto it = images_by_hash_.find(hash);
    if (it != images_by_hash_.end()) {
        return it->second->available_thumbnail_sizes;
    }
    return {};
}

//==============================================================================
// Scan State Management
//==============================================================================

void StateStore::start_scan(const std::string& directory_path) {
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        scan_state_.is_active = true;
        scan_state_.current_count = 0;
        scan_state_.total_count = 0;
        scan_state_.status_message = "Starting scan...";
        scan_state_.directory_path = directory_path;
        scan_state_.start_time = std::chrono::steady_clock::now();
        scan_state_.last_update = scan_state_.start_time;
    }
    
    publish_scan_event(StateEventType::SCAN_STARTED);
}

void StateStore::update_scan_progress(int current, int total, const std::string& status) {
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        scan_state_.current_count = current;
        scan_state_.total_count = total;
        scan_state_.status_message = status;
        scan_state_.last_update = std::chrono::steady_clock::now();
    }
    
    publish_scan_event(StateEventType::SCAN_PROGRESS);
}

void StateStore::complete_scan() {
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        scan_state_.is_active = false;
        scan_state_.status_message = "Scan completed";
        scan_state_.last_update = std::chrono::steady_clock::now();
    }
    
    publish_scan_event(StateEventType::SCAN_COMPLETED);
}

void StateStore::cancel_scan() {
    {
        std::unique_lock<std::shared_mutex> lock(state_mutex_);
        scan_state_.is_active = false;
        scan_state_.status_message = "Scan cancelled";
        scan_state_.last_update = std::chrono::steady_clock::now();
    }
    
    publish_scan_event(StateEventType::SCAN_CANCELLED);
}

ScanState StateStore::get_scan_state() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    return scan_state_;
}

//==============================================================================
// Cache Management
//==============================================================================

void StateStore::clear_thumbnail_cache() {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    thumbnail_cache_.clear();
    
    // Publish cache cleared event
    publish_cache_event(StateEventType::CACHE_CLEARED);
}

void StateStore::set_cache_memory_limit(size_t max_memory_mb) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    thumbnail_cache_.set_max_memory(max_memory_mb * 1024 * 1024); // Convert MB to bytes
}

void StateStore::set_cache_item_limit(size_t max_items) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    thumbnail_cache_.set_max_items(max_items);
}

CacheConfig StateStore::get_cache_config() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    auto stats = thumbnail_cache_.get_stats();
    CacheConfig config;
    config.max_memory_mb = stats.max_memory_bytes / (1024 * 1024);
    config.max_items = 1000; // Would need to track this in cache provider
    config.enable_stats = true;
    
    return config;
}

void StateStore::update_cache_config(const CacheConfig& config) {
    std::unique_lock<std::shared_mutex> lock(state_mutex_);
    thumbnail_cache_.set_max_memory(config.max_memory_bytes());
    thumbnail_cache_.set_max_items(config.max_items);
}

StateStore::CacheStats StateStore::get_cache_stats() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    CacheStats stats;
    stats.image_count = images_by_hash_.size();
    
    // Get cache provider stats
    auto cache_stats = thumbnail_cache_.get_stats();
    stats.thumbnail_count = cache_stats.total_items;
    stats.cache_memory_usage = cache_stats.total_memory_bytes;
    stats.cache_hit_count = cache_stats.hit_count;
    stats.cache_miss_count = cache_stats.miss_count;
    stats.cache_hit_ratio = cache_stats.hit_ratio;
    
    return stats;
}

//==============================================================================
// Helper Methods
//==============================================================================

void StateStore::publish_image_event(StateEventType type, const ImageState& state) {
    ImageEvent event(type, state.metadata.hash, state.metadata.path, state.metadata.aspect_ratio);
    event_bus_.publish(event);
}

void StateStore::publish_thumbnail_event(StateEventType type, const std::string& hash,
                                        const CachedThumbnail& thumbnail) {
    ThumbnailEvent event(type, hash, thumbnail.width, thumbnail.height, thumbnail.size);
    event_bus_.publish(event);
}

void StateStore::publish_scan_event(StateEventType type) {
    ScanEvent event(type, scan_state_.current_count, scan_state_.total_count, scan_state_.status_message);
    event_bus_.publish(event);
}

void StateStore::publish_cache_event(StateEventType type, const std::string& cache_key, 
                                   size_t memory_freed, size_t items_affected) {
    CacheEvent event(type, cache_key, memory_freed, items_affected);
    event_bus_.publish(event);
}

std::string StateStore::make_thumbnail_cache_key(const std::string& hash, int size) const {
    return hash + ":" + std::to_string(size);
}

void StateStore::setup_cache_eviction_callback() {
    // Set up callback to publish cache eviction events
    thumbnail_cache_.set_eviction_callback([this](const std::string& key, const ThumbnailCacheData& data) {
        // Calculate memory freed (approximate)
        size_t memory_freed = data.jpeg_data.size();
        publish_cache_event(StateEventType::CACHE_EVICTED, key, memory_freed, 1);
    });
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s