/*
 * mvc_core_test.cpp - Tests for core MVC architecture components (without GUI dependencies)
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <chrono>
#include <thread>

#include "state_store.hpp"
#include "event_bus.hpp"

// Test utilities
class TestLogger {
public:
    static void log(const std::string& test_name, bool passed) {
        std::cout << "Test: " << test_name << "... " 
                  << (passed ? "PASSED" : "FAILED") << std::endl;
        if (!passed) {
            test_failures_++;
        }
        total_tests_++;
    }
    
    static void summary() {
        std::cout << "\nTest Summary: " << (total_tests_ - test_failures_) 
                  << "/" << total_tests_ << " tests passed";
        if (test_failures_ > 0) {
            std::cout << " (" << test_failures_ << " failures)";
        }
        std::cout << std::endl;
    }
    
    static bool all_passed() { return test_failures_ == 0; }

private:
    static int total_tests_;
    static int test_failures_;
};

int TestLogger::total_tests_ = 0;
int TestLogger::test_failures_ = 0;

// Test core state management functionality
void test_state_store_mvc_patterns() {
    auto state_store = std::make_shared<StateStore>();
    
    // Test that state store can be used for MVC patterns
    bool creation_success = (state_store != nullptr);
    TestLogger::log("StateStore creation for MVC", creation_success);
    
    // Test event bus availability
    bool has_event_bus = true;
    try {
        auto& bus = state_store->get_event_bus();
        (void)bus; // Suppress unused variable warning
    } catch (...) {
        has_event_bus = false;
    }
    TestLogger::log("StateStore provides EventBus", has_event_bus);
    
    // Test image metadata management
    StateImageInfo test_image;
    test_image.path = "/mvc/test/image.jpg";
    test_image.hash = "mvchash123";
    test_image.aspect_ratio = 1.6;
    
    state_store->add_image_metadata(test_image);
    
    auto retrieved_state = state_store->get_image_state("mvchash123");
    bool image_stored = (retrieved_state != nullptr && 
                        retrieved_state->metadata.path == "/mvc/test/image.jpg");
    TestLogger::log("StateStore image metadata management", image_stored);
}

// Test event-driven communication patterns used in MVC
void test_mvc_event_patterns() {
    auto state_store = std::make_shared<StateStore>();
    
    // Test observer pattern implementation
    bool metadata_event_received = false;
    bool thumbnail_event_received = false;
    
    // Subscribe to different event types (as controllers would)
    auto metadata_subscription = state_store->get_event_bus().subscribe(
        StateEventType::IMAGE_METADATA_ADDED,
        [&](const StateEvent& event) {
            metadata_event_received = true;
        }
    );
    
    auto thumbnail_subscription = state_store->get_event_bus().subscribe(
        StateEventType::THUMBNAIL_READY,
        [&](const StateEvent& event) {
            thumbnail_event_received = true;
        }
    );
    
    // Trigger events
    StateImageInfo test_image;
    test_image.hash = "eventhash789";
    test_image.path = "/mvc/event/test.jpg";
    state_store->add_image_metadata(test_image);
    
    std::vector<uint8_t> thumbnail_data = {1, 2, 3, 4, 5};
    state_store->add_thumbnail("eventhash789", 128, thumbnail_data, 128, 128);
    
    // Give time for event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    TestLogger::log("MVC event pattern - metadata events", metadata_event_received);
    TestLogger::log("MVC event pattern - thumbnail events", thumbnail_event_received);
    
    // Cleanup subscriptions
    state_store->get_event_bus().unsubscribe(metadata_subscription);
    state_store->get_event_bus().unsubscribe(thumbnail_subscription);
}

// Test separation of concerns in state management
void test_state_separation_concerns() {
    auto state_store = std::make_shared<StateStore>();
    
    // Test that different types of state are managed separately
    
    // Image metadata state
    StateImageInfo image1;
    image1.hash = "img1";
    image1.path = "/path/img1.jpg";
    state_store->add_image_metadata(image1);
    
    StateImageInfo image2;
    image2.hash = "img2";
    image2.path = "/path/img2.jpg";
    state_store->add_image_metadata(image2);
    
    bool has_multiple_images = (state_store->get_image_count() == 2);
    TestLogger::log("State separation - multiple images", has_multiple_images);
    
    // Thumbnail state (separate from metadata)
    std::vector<uint8_t> thumb_data = {10, 20, 30};
    state_store->add_thumbnail("img1", 64, thumb_data, 64, 64);
    
    bool has_thumbnail = state_store->has_thumbnail("img1", 64);
    bool no_thumbnail_for_other = !state_store->has_thumbnail("img2", 64);
    
    TestLogger::log("State separation - thumbnail independence", 
                   has_thumbnail && no_thumbnail_for_other);
    
    // Scan state (separate from both)
    state_store->start_scan("/test/directory");
    auto scan_state = state_store->get_scan_state();
    bool scan_active = scan_state.is_active;
    
    state_store->complete_scan();
    scan_state = state_store->get_scan_state();
    bool scan_completed = !scan_state.is_active;
    
    TestLogger::log("State separation - scan state independence", 
                   scan_active && scan_completed);
}

// Test cache management for UI performance
void test_mvc_cache_patterns() {
    auto state_store = std::make_shared<StateStore>();
    
    // Test cache configuration and limits (important for view performance)
    CacheConfig config;
    config.max_memory_mb = 10;
    config.max_items = 100;
    
    state_store->update_cache_config(config);
    
    auto retrieved_config = state_store->get_cache_config();
    bool config_applied = (retrieved_config.max_memory_mb == 10 && 
                          retrieved_config.max_items == 100);
    TestLogger::log("MVC cache configuration", config_applied);
    
    // Test cache behavior with multiple thumbnails
    for (int i = 0; i < 5; ++i) {
        std::string hash = "cachehash" + std::to_string(i);
        std::vector<uint8_t> data(1000, static_cast<uint8_t>(i)); // 1KB each
        state_store->add_thumbnail(hash, 128, data, 128, 128);
    }
    
    auto stats = state_store->get_cache_stats();
    bool has_cached_items = (stats.thumbnail_count == 5);
    TestLogger::log("MVC cache population", has_cached_items);
    
    // Test cache retrieval (important for view updates)
    auto cached_thumbnail = state_store->get_thumbnail("cachehash0", 128);
    bool cache_retrieval = (cached_thumbnail != nullptr);
    TestLogger::log("MVC cache retrieval", cache_retrieval);
}

// Test concurrent access patterns (important for threaded MVC)
void test_mvc_concurrency_patterns() {
    auto state_store = std::make_shared<StateStore>();
    
    // Test concurrent image additions (as scan controller might do)
    std::vector<std::thread> threads;
    std::atomic<int> successful_additions(0);
    
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back([&, i]() {
            for (int j = 0; j < 5; ++j) {
                StateImageInfo image;
                image.hash = "concurrent" + std::to_string(i) + "_" + std::to_string(j);
                image.path = "/concurrent/" + image.hash + ".jpg";
                
                state_store->add_image_metadata(image);
                successful_additions++;
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    bool all_added = (successful_additions == 15 && state_store->get_image_count() == 15);
    TestLogger::log("MVC concurrent image additions", all_added);
    
    // Test concurrent event subscriptions
    std::atomic<int> events_received(0);
    std::vector<uint64_t> subscription_ids;
    
    // Multiple subscribers (simulating multiple view components)
    for (int i = 0; i < 3; ++i) {
        auto sub_id = state_store->get_event_bus().subscribe(
            StateEventType::THUMBNAIL_READY,
            [&](const StateEvent& event) {
                events_received++;
            }
        );
        subscription_ids.push_back(sub_id);
    }
    
    // Trigger event
    std::vector<uint8_t> data = {1, 2, 3};
    state_store->add_thumbnail("concurrent1_0", 256, data, 256, 256);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    bool all_notified = (events_received == 3); // All 3 subscribers should get event
    TestLogger::log("MVC concurrent event notifications", all_notified);
    
    // Cleanup
    for (auto sub_id : subscription_ids) {
        state_store->get_event_bus().unsubscribe(sub_id);
    }
}

int main() {
    std::cout << "Running MVC Core Components Tests...\n" << std::endl;
    
    test_state_store_mvc_patterns();
    test_mvc_event_patterns();
    test_state_separation_concerns();
    test_mvc_cache_patterns();
    test_mvc_concurrency_patterns();
    
    TestLogger::summary();
    
    return TestLogger::all_passed() ? 0 : 1;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s