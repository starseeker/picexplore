/*
 * cache_provider.hpp - Unified LRU cache provider for picexplore
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

#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <chrono>
#include <functional>
#include <atomic>
#include <string>

/**
 * Generic cache item that wraps cached data with metadata
 */
template<typename T>
struct CacheItem {
    T data;
    std::string key;
    size_t size_bytes;
    std::chrono::steady_clock::time_point created_at;
    std::chrono::steady_clock::time_point last_accessed;
    
    CacheItem() = default;
    
    CacheItem(const std::string& k, T&& d, size_t size = 0)
        : key(k), data(std::move(d)), size_bytes(size)
        , created_at(std::chrono::steady_clock::now())
        , last_accessed(created_at) {}
        
    CacheItem(const std::string& k, const T& d, size_t size = 0)
        : key(k), data(d), size_bytes(size)
        , created_at(std::chrono::steady_clock::now())
        , last_accessed(created_at) {}
};

/**
 * Cache statistics for monitoring
 */
struct CacheStats {
    size_t total_items = 0;
    size_t total_memory_bytes = 0;
    size_t max_memory_bytes = 0;
    size_t hit_count = 0;
    size_t miss_count = 0;
    size_t eviction_count = 0;
    double hit_ratio = 0.0;
    
    void update_hit_ratio() {
        size_t total_requests = hit_count + miss_count;
        if (total_requests > 0) {
            hit_ratio = static_cast<double>(hit_count) / total_requests;
        }
    }
};

/**
 * Cache eviction callback - called when items are evicted from cache
 */
template<typename T>
using EvictionCallback = std::function<void(const std::string&, const T&)>;

/**
 * LRU Cache Provider with memory limits and automatic eviction
 * 
 * Thread-safe cache implementation that:
 * - Uses LRU (Least Recently Used) eviction policy
 * - Enforces configurable memory limits
 * - Provides cache statistics and monitoring
 * - Supports eviction callbacks for cleanup
 * - Works with any data type T
 */
template<typename T>
class CacheProvider {
public:
    /**
     * Constructor
     * @param max_memory Maximum memory usage in bytes (0 = unlimited)
     * @param max_items Maximum number of items (0 = unlimited)
     */
    explicit CacheProvider(size_t max_memory = 0, size_t max_items = 0);
    
    /**
     * Destructor - clears all cached items
     */
    ~CacheProvider();

    //==========================================================================
    // Cache Operations
    //==========================================================================
    
    /**
     * Insert or update an item in the cache
     * @param key Cache key (must be unique)
     * @param data Data to cache
     * @param size_bytes Size of data in bytes (for memory limit enforcement)
     * @return true if item was cached, false if rejected (e.g., too large)
     */
    bool put(const std::string& key, T data, size_t size_bytes = 0);
    
    /**
     * Retrieve an item from the cache
     * @param key Cache key
     * @return Pointer to cached item, or nullptr if not found
     * Updates LRU order on access
     */
    std::shared_ptr<CacheItem<T>> get(const std::string& key);
    
    /**
     * Check if an item exists in the cache without affecting LRU order
     * @param key Cache key
     * @return true if item exists
     */
    bool contains(const std::string& key) const;
    
    /**
     * Remove an item from the cache
     * @param key Cache key
     * @return true if item was removed, false if not found
     */
    bool remove(const std::string& key);
    
    /**
     * Remove all items matching a predicate
     * @param predicate Function that returns true for items to remove
     * @return Number of items removed
     */
    size_t remove_if(std::function<bool(const std::string&, const T&)> predicate);
    
    /**
     * Clear all items from the cache
     */
    void clear();

    //==========================================================================
    // Configuration
    //==========================================================================
    
    /**
     * Set maximum memory usage
     * @param max_memory Maximum memory in bytes (0 = unlimited)
     * May trigger immediate eviction if current usage exceeds new limit
     */
    void set_max_memory(size_t max_memory);
    
    /**
     * Set maximum number of items
     * @param max_items Maximum items (0 = unlimited)
     * May trigger immediate eviction if current count exceeds new limit
     */
    void set_max_items(size_t max_items);
    
    /**
     * Set eviction callback
     * @param callback Function called when items are evicted
     */
    void set_eviction_callback(EvictionCallback<T> callback);

    //==========================================================================
    // Monitoring
    //==========================================================================
    
    /**
     * Get cache statistics
     */
    CacheStats get_stats() const;
    
    /**
     * Get current memory usage in bytes
     */
    size_t get_memory_usage() const;
    
    /**
     * Get current number of items
     */
    size_t get_item_count() const;

private:
    // LRU implementation using list + unordered_map
    using LRUList = std::list<std::string>;
    using LRUIterator = typename LRUList::iterator;
    
    struct CacheEntry {
        std::shared_ptr<CacheItem<T>> item;
        LRUIterator lru_iter;
        
        CacheEntry(std::shared_ptr<CacheItem<T>> it, LRUIterator iter)
            : item(std::move(it)), lru_iter(iter) {}
    };
    
    mutable std::mutex cache_mutex_;
    
    // Cache storage
    std::unordered_map<std::string, CacheEntry> cache_map_;
    LRUList lru_list_;  // Most recently used items at front
    
    // Configuration
    size_t max_memory_bytes_;
    size_t max_items_;
    EvictionCallback<T> eviction_callback_;
    
    // Statistics
    mutable std::atomic<size_t> total_memory_bytes_{0};
    mutable std::atomic<size_t> hit_count_{0};
    mutable std::atomic<size_t> miss_count_{0};
    mutable std::atomic<size_t> eviction_count_{0};
    
    // Helper methods
    void move_to_front(const std::string& key, CacheEntry& entry);
    void evict_lru_items();
    void evict_item(const std::string& key);
    bool should_evict() const;
    size_t calculate_size(const T& data, size_t provided_size) const;
};

//==============================================================================
// Template Implementation
//==============================================================================

template<typename T>
CacheProvider<T>::CacheProvider(size_t max_memory, size_t max_items)
    : max_memory_bytes_(max_memory), max_items_(max_items) {
}

template<typename T>
CacheProvider<T>::~CacheProvider() {
    clear();
}

template<typename T>
bool CacheProvider<T>::put(const std::string& key, T data, size_t size_bytes) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    size_t actual_size = calculate_size(data, size_bytes);
    
    // Check if item would exceed memory limit
    if (max_memory_bytes_ > 0 && actual_size > max_memory_bytes_) {
        return false; // Item too large for cache
    }
    
    // Check if key already exists
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Update existing item
        size_t old_size = it->second.item->size_bytes;
        it->second.item->data = std::move(data);
        it->second.item->size_bytes = actual_size;
        it->second.item->last_accessed = std::chrono::steady_clock::now();
        
        // Update memory tracking
        total_memory_bytes_.fetch_add(actual_size - old_size);
        
        // Move to front of LRU
        move_to_front(key, it->second);
    } else {
        // Add new item
        auto cache_item = std::make_shared<CacheItem<T>>(key, std::move(data), actual_size);
        
        // Add to front of LRU list
        lru_list_.push_front(key);
        
        // Add to map
        cache_map_.emplace(key, CacheEntry(cache_item, lru_list_.begin()));
        
        // Update memory tracking
        total_memory_bytes_.fetch_add(actual_size);
    }
    
    // Evict items if necessary
    evict_lru_items();
    
    return true;
}

template<typename T>
std::shared_ptr<CacheItem<T>> CacheProvider<T>::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Update access time
        it->second.item->last_accessed = std::chrono::steady_clock::now();
        
        // Move to front of LRU
        move_to_front(key, it->second);
        
        // Update statistics
        hit_count_.fetch_add(1);
        
        return it->second.item;
    }
    
    // Update statistics
    miss_count_.fetch_add(1);
    
    return nullptr;
}

template<typename T>
bool CacheProvider<T>::contains(const std::string& key) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_map_.find(key) != cache_map_.end();
}

template<typename T>
bool CacheProvider<T>::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        evict_item(key);
        return true;
    }
    return false;
}

template<typename T>
size_t CacheProvider<T>::remove_if(std::function<bool(const std::string&, const T&)> predicate) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    std::vector<std::string> keys_to_remove;
    
    for (const auto& pair : cache_map_) {
        if (predicate(pair.first, pair.second.item->data)) {
            keys_to_remove.push_back(pair.first);
        }
    }
    
    for (const auto& key : keys_to_remove) {
        evict_item(key);
    }
    
    return keys_to_remove.size();
}

template<typename T>
void CacheProvider<T>::clear() {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    // Call eviction callback for all items
    if (eviction_callback_) {
        for (const auto& pair : cache_map_) {
            eviction_callback_(pair.first, pair.second.item->data);
        }
    }
    
    cache_map_.clear();
    lru_list_.clear();
    total_memory_bytes_.store(0);
}

template<typename T>
void CacheProvider<T>::set_max_memory(size_t max_memory) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    max_memory_bytes_ = max_memory;
    evict_lru_items();
}

template<typename T>
void CacheProvider<T>::set_max_items(size_t max_items) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    max_items_ = max_items;
    evict_lru_items();
}

template<typename T>
void CacheProvider<T>::set_eviction_callback(EvictionCallback<T> callback) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    eviction_callback_ = std::move(callback);
}

template<typename T>
CacheStats CacheProvider<T>::get_stats() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    
    CacheStats stats;
    stats.total_items = cache_map_.size();
    stats.total_memory_bytes = total_memory_bytes_.load();
    stats.max_memory_bytes = max_memory_bytes_;
    stats.hit_count = hit_count_.load();
    stats.miss_count = miss_count_.load();
    stats.eviction_count = eviction_count_.load();
    stats.update_hit_ratio();
    
    return stats;
}

template<typename T>
size_t CacheProvider<T>::get_memory_usage() const {
    return total_memory_bytes_.load();
}

template<typename T>
size_t CacheProvider<T>::get_item_count() const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return cache_map_.size();
}

//==============================================================================
// Private Helper Methods
//==============================================================================

template<typename T>
void CacheProvider<T>::move_to_front(const std::string& key, CacheEntry& entry) {
    // Remove from current position
    lru_list_.erase(entry.lru_iter);
    
    // Add to front
    lru_list_.push_front(key);
    entry.lru_iter = lru_list_.begin();
}

template<typename T>
void CacheProvider<T>::evict_lru_items() {
    while (should_evict() && !lru_list_.empty()) {
        // Evict least recently used item (back of list)
        std::string key = lru_list_.back();
        evict_item(key);
    }
}

template<typename T>
void CacheProvider<T>::evict_item(const std::string& key) {
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // Call eviction callback
        if (eviction_callback_) {
            eviction_callback_(key, it->second.item->data);
        }
        
        // Update memory tracking
        total_memory_bytes_.fetch_sub(it->second.item->size_bytes);
        
        // Remove from LRU list
        lru_list_.erase(it->second.lru_iter);
        
        // Remove from map
        cache_map_.erase(it);
        
        // Update statistics
        eviction_count_.fetch_add(1);
    }
}

template<typename T>
bool CacheProvider<T>::should_evict() const {
    bool memory_exceeded = (max_memory_bytes_ > 0 && 
                           total_memory_bytes_.load() > max_memory_bytes_);
    bool items_exceeded = (max_items_ > 0 && 
                          cache_map_.size() > max_items_);
    return memory_exceeded || items_exceeded;
}

template<typename T>
size_t CacheProvider<T>::calculate_size(const T& data, size_t provided_size) const {
    if (provided_size > 0) {
        return provided_size;
    }
    
    // Default size calculation - can be specialized for specific types
    return sizeof(T);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s