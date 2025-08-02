/*
 * event_bus.cpp - Event system implementation for picexplore StateStore
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "event_bus.hpp"
#include <algorithm>
#include <chrono>

EventBus::EventBus() : next_subscription_id_(1) {
}

EventBus::~EventBus() {
    clear_all_subscriptions();
}

uint64_t EventBus::subscribe(StateEventType event_type, EventHandler handler) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    uint64_t sub_id = get_next_subscription_id();
    subscriptions_.emplace_back(sub_id, event_type, std::move(handler), false);
    
    return sub_id;
}

uint64_t EventBus::subscribe_all(EventHandler handler) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    uint64_t sub_id = get_next_subscription_id();
    // Use IMAGE_METADATA_ADDED as placeholder - is_all_events flag determines behavior
    subscriptions_.emplace_back(sub_id, StateEventType::IMAGE_METADATA_ADDED, std::move(handler), true);
    
    return sub_id;
}

void EventBus::unsubscribe(uint64_t subscription_id) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [subscription_id](const Subscription& sub) {
                return sub.id == subscription_id;
            }),
        subscriptions_.end()
    );
}

void EventBus::publish(const StateEvent& event) {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    // Call all matching subscriptions
    for (const auto& subscription : subscriptions_) {
        // Call if it's an all-events subscription or if the event type matches
        if (subscription.is_all_events || subscription.event_type == event.type) {
            try {
                subscription.handler(event);
            } catch (const std::exception& e) {
                // Log error but continue processing other handlers
                // In a full implementation, this would use the logging system
                // For now, we'll silently continue to avoid breaking the event system
            }
        }
    }
}

size_t EventBus::get_subscription_count() const {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    return subscriptions_.size();
}

void EventBus::clear_all_subscriptions() {
    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    subscriptions_.clear();
}

uint64_t EventBus::get_next_subscription_id() {
    return next_subscription_id_.fetch_add(1);
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s