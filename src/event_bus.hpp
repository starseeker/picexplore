/*
 * event_bus.hpp - Event system for picexplore StateStore
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

#include <functional>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include <string>
#include <chrono>

// Forward declarations
struct ImageInfo;
struct StateEvent;
struct ImageEvent;
struct ThumbnailEvent;
struct ScanEvent;

/**
 * Event types for the StateStore event system
 */
enum class StateEventType {
    IMAGE_METADATA_ADDED,    // New image metadata available
    IMAGE_METADATA_UPDATED,  // Existing image metadata changed
    THUMBNAIL_READY,         // Thumbnail generated and available
    THUMBNAIL_UPDATED,       // Existing thumbnail updated
    SCAN_STARTED,           // Directory scan started
    SCAN_PROGRESS,          // Scan progress update
    SCAN_COMPLETED,         // Directory scan completed
    SCAN_CANCELLED          // Directory scan cancelled
};

/**
 * Base event data structure
 */
struct StateEvent {
    StateEventType type;
    std::string timestamp;
    
    StateEvent(StateEventType t) : type(t) {
        // Simple timestamp - could be enhanced with chrono
        timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    virtual ~StateEvent() = default;
};

/**
 * Image-related event data
 */
struct ImageEvent : public StateEvent {
    std::string hash;
    std::string path;
    double aspect_ratio = 1.0;
    
    ImageEvent(StateEventType t, const std::string& h, const std::string& p, double ar = 1.0)
        : StateEvent(t), hash(h), path(p), aspect_ratio(ar) {}
};

/**
 * Thumbnail-related event data
 */
struct ThumbnailEvent : public StateEvent {
    std::string hash;
    int width;
    int height;
    int size; // thumbnail size category (32, 64, 128, etc.)
    
    ThumbnailEvent(StateEventType t, const std::string& h, int w, int h_val, int s)
        : StateEvent(t), hash(h), width(w), height(h_val), size(s) {}
};

/**
 * Scan progress event data
 */
struct ScanEvent : public StateEvent {
    int current_count = 0;
    int total_count = 0;
    std::string status_message;
    
    ScanEvent(StateEventType t, int current = 0, int total = 0, const std::string& msg = "")
        : StateEvent(t), current_count(current), total_count(total), status_message(msg) {}
};

/**
 * Event handler function type
 */
using EventHandler = std::function<void(const StateEvent&)>;

/**
 * EventBus - Observer pattern implementation for StateStore
 * 
 * Thread-safe event bus that allows components to subscribe to state changes
 * and receive notifications when the StateStore is updated.
 */
class EventBus {
public:
    EventBus();
    ~EventBus();

    /**
     * Subscribe to events of a specific type
     * Returns a subscription ID that can be used to unsubscribe
     */
    uint64_t subscribe(StateEventType event_type, EventHandler handler);
    
    /**
     * Subscribe to all event types
     * Returns a subscription ID that can be used to unsubscribe
     */
    uint64_t subscribe_all(EventHandler handler);
    
    /**
     * Unsubscribe using subscription ID
     */
    void unsubscribe(uint64_t subscription_id);
    
    /**
     * Publish an event to all subscribers
     * Thread-safe - can be called from any thread
     */
    void publish(const StateEvent& event);
    
    /**
     * Get subscription count for debugging
     */
    size_t get_subscription_count() const;
    
    /**
     * Clear all subscriptions (useful for shutdown)
     */
    void clear_all_subscriptions();

private:
    struct Subscription {
        uint64_t id;
        StateEventType event_type;
        EventHandler handler;
        bool is_all_events = false; // true if this subscription is for all events
        
        Subscription(uint64_t sub_id, StateEventType type, EventHandler h, bool all = false)
            : id(sub_id), event_type(type), handler(std::move(h)), is_all_events(all) {}
    };
    
    mutable std::mutex subscriptions_mutex_;
    std::vector<Subscription> subscriptions_;
    std::atomic<uint64_t> next_subscription_id_;
    
    // Helper method to get next unique subscription ID
    uint64_t get_next_subscription_id();
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s