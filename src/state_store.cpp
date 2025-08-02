/*
 * state_store.cpp - Central state management implementation for picexplore
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "state_store.hpp"
#include <algorithm>
#include <shared_mutex>

StateStore::StateStore() {
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
        
        auto it = thumbnail_cache_.find(cache_key);
        if (it == thumbnail_cache_.end()) {
            // New thumbnail
            auto thumbnail = std::make_shared<CachedThumbnail>(data, width, height, size, cache_key);
            thumbnail_cache_[cache_key] = thumbnail;
            is_new_thumbnail = true;
            event_type = StateEventType::THUMBNAIL_READY;
        } else {
            // Update existing thumbnail
            it->second->data = data;
            it->second->width = width;
            it->second->height = height;
            it->second->last_accessed = std::chrono::steady_clock::now();
        }
        
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
    
    // Publish event outside of lock
    ThumbnailEvent thumbnail_event(event_type, hash, width, height, size);
    event_bus_.publish(thumbnail_event);
}

std::shared_ptr<const CachedThumbnail> StateStore::get_thumbnail(const std::string& hash, int size) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    std::string cache_key = make_thumbnail_cache_key(hash, size);
    auto it = thumbnail_cache_.find(cache_key);
    if (it != thumbnail_cache_.end()) {
        // Update last accessed time (const_cast is safe here for cache management)
        const_cast<CachedThumbnail*>(it->second.get())->last_accessed = 
            std::chrono::steady_clock::now();
        return it->second;
    }
    return nullptr;
}

bool StateStore::has_thumbnail(const std::string& hash, int size) const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    std::string cache_key = make_thumbnail_cache_key(hash, size);
    return thumbnail_cache_.find(cache_key) != thumbnail_cache_.end();
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
}

StateStore::CacheStats StateStore::get_cache_stats() const {
    std::shared_lock<std::shared_mutex> lock(state_mutex_);
    
    CacheStats stats;
    stats.image_count = images_by_hash_.size();
    stats.thumbnail_count = thumbnail_cache_.size();
    
    // Calculate approximate memory usage
    for (const auto& pair : thumbnail_cache_) {
        stats.cache_memory_usage += pair.second->data.size();
    }
    
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

std::string StateStore::make_thumbnail_cache_key(const std::string& hash, int size) const {
    return hash + ":" + std::to_string(size);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s