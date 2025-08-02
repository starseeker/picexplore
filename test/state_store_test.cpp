/*
 * state_store_test.cpp - Unit tests for StateStore and EventBus
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

#include "state_store.hpp"
#include "event_bus.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <vector>
#include <cassert>

// Simple test framework
class StateStoreTest {
public:
    static void run_all_tests() {
        std::cout << "Running StateStore and EventBus tests...\n";
        
        test_event_bus_basic();
        test_event_bus_subscription_management();
        test_state_store_image_metadata();
        test_state_store_thumbnail_management();
        test_state_store_scan_state();
        test_state_store_events_integration();
        test_cache_invalidation();
        test_cache_configuration();
        test_concurrent_access();
        
        std::cout << "All tests passed!\n";
    }
    
private:
    static void test_event_bus_basic() {
        std::cout << "Test: EventBus basic functionality... ";
        
        EventBus event_bus;
        std::atomic<int> event_count{0};
        std::atomic<int> image_events{0};
        std::atomic<int> scan_events{0};
        
        // Subscribe to specific event type
        auto sub1 = event_bus.subscribe(StateEventType::IMAGE_METADATA_ADDED, 
            [&event_count, &image_events](const StateEvent& event) {
                event_count++;
                image_events++;
            });
        
        // Subscribe to all events
        auto sub2 = event_bus.subscribe_all(
            [&event_count](const StateEvent& event) {
                event_count++;
            });
        
        // Publish an image event
        ImageEvent img_event(StateEventType::IMAGE_METADATA_ADDED, "hash123", "/path/to/image.jpg", 1.5);
        event_bus.publish(img_event);
        
        // Should trigger both subscriptions
        assert(event_count.load() == 2);
        assert(image_events.load() == 1);
        
        // Publish a scan event
        ScanEvent scan_event(StateEventType::SCAN_STARTED, 0, 100, "Starting scan");
        event_bus.publish(scan_event);
        
        // Should trigger only the all-events subscription
        assert(event_count.load() == 3);
        assert(image_events.load() == 1);
        
        // Test unsubscribe
        event_bus.unsubscribe(sub1);
        
        ImageEvent img_event2(StateEventType::IMAGE_METADATA_ADDED, "hash456", "/path/to/image2.jpg", 2.0);
        event_bus.publish(img_event2);
        
        // Should trigger only the all-events subscription
        assert(event_count.load() == 4);
        assert(image_events.load() == 1);
        
        std::cout << "PASSED\n";
    }
    
    static void test_event_bus_subscription_management() {
        std::cout << "Test: EventBus subscription management... ";
        
        EventBus event_bus;
        
        assert(event_bus.get_subscription_count() == 0);
        
        auto sub1 = event_bus.subscribe(StateEventType::IMAGE_METADATA_ADDED, 
            [](const StateEvent&) {});
        auto sub2 = event_bus.subscribe_all([](const StateEvent&) {});
        
        assert(event_bus.get_subscription_count() == 2);
        
        event_bus.unsubscribe(sub1);
        assert(event_bus.get_subscription_count() == 1);
        
        event_bus.clear_all_subscriptions();
        assert(event_bus.get_subscription_count() == 0);
        
        std::cout << "PASSED\n";
    }
    
    static void test_state_store_image_metadata() {
        std::cout << "Test: StateStore image metadata management... ";
        
        StateStore state_store;
        
        // Test initial state
        assert(state_store.get_image_count() == 0);
        assert(!state_store.has_image("hash123"));
        
        // Add image metadata
        StateImageInfo info1;
        info1.path = "/path/to/image1.jpg";
        info1.hash = "hash123";
        info1.aspect_ratio = 1.5;
        
        state_store.add_image_metadata(info1);
        
        // Verify image was added
        assert(state_store.get_image_count() == 1);
        assert(state_store.has_image("hash123"));
        
        auto image_state = state_store.get_image_state("hash123");
        assert(image_state != nullptr);
        assert(image_state->metadata.path == "/path/to/image1.jpg");
        assert(image_state->metadata.hash == "hash123");
        assert(image_state->metadata.aspect_ratio == 1.5);
        
        // Test get_all_images
        auto all_images = state_store.get_all_images();
        assert(all_images.size() == 1);
        assert(all_images[0].metadata.hash == "hash123");
        
        // Add another image
        StateImageInfo info2;
        info2.path = "/path/to/image2.jpg";
        info2.hash = "hash456";
        info2.aspect_ratio = 2.0;
        
        state_store.add_image_metadata(info2);
        assert(state_store.get_image_count() == 2);
        
        std::cout << "PASSED\n";
    }
    
    static void test_state_store_thumbnail_management() {
        std::cout << "Test: StateStore thumbnail management... ";
        
        StateStore state_store;
        
        // Add image first
        StateImageInfo info;
        info.path = "/path/to/image.jpg";
        info.hash = "hash123";
        info.aspect_ratio = 1.5;
        state_store.add_image_metadata(info);
        
        // Test initial thumbnail state
        assert(!state_store.has_thumbnail("hash123", 256));
        assert(state_store.get_available_thumbnail_sizes("hash123").empty());
        
        // Add thumbnail
        std::vector<uint8_t> thumbnail_data = {1, 2, 3, 4, 5}; // Dummy JPEG data
        state_store.add_thumbnail("hash123", 256, thumbnail_data, 256, 192);
        
        // Verify thumbnail was added
        assert(state_store.has_thumbnail("hash123", 256));
        
        auto cached_thumb = state_store.get_thumbnail("hash123", 256);
        assert(cached_thumb != nullptr);
        assert(cached_thumb->data == thumbnail_data);
        assert(cached_thumb->width == 256);
        assert(cached_thumb->height == 192);
        assert(cached_thumb->size == 256);
        
        // Check available sizes
        auto sizes = state_store.get_available_thumbnail_sizes("hash123");
        assert(sizes.size() == 1);
        assert(sizes[0] == 256);
        
        // Add another thumbnail size
        std::vector<uint8_t> thumb_data_128 = {6, 7, 8, 9, 10};
        state_store.add_thumbnail("hash123", 128, thumb_data_128, 128, 96);
        
        sizes = state_store.get_available_thumbnail_sizes("hash123");
        assert(sizes.size() == 2);
        assert(std::find(sizes.begin(), sizes.end(), 128) != sizes.end());
        assert(std::find(sizes.begin(), sizes.end(), 256) != sizes.end());
        
        // Test mark_thumbnails_generated
        state_store.mark_thumbnails_generated("hash123", {128, 256, 512});
        auto image_state = state_store.get_image_state("hash123");
        assert(image_state->thumbnails_generated);
        assert(!image_state->is_being_processed);
        
        std::cout << "PASSED\n";
    }
    
    static void test_state_store_scan_state() {
        std::cout << "Test: StateStore scan state management... ";
        
        StateStore state_store;
        
        // Test initial scan state
        auto scan_state = state_store.get_scan_state();
        assert(!scan_state.is_active);
        
        // Start scan
        state_store.start_scan("/path/to/directory");
        scan_state = state_store.get_scan_state();
        assert(scan_state.is_active);
        assert(scan_state.directory_path == "/path/to/directory");
        assert(scan_state.status_message == "Starting scan...");
        
        // Update progress
        state_store.update_scan_progress(50, 100, "Processing images...");
        scan_state = state_store.get_scan_state();
        assert(scan_state.current_count == 50);
        assert(scan_state.total_count == 100);
        assert(scan_state.status_message == "Processing images...");
        
        // Complete scan
        state_store.complete_scan();
        scan_state = state_store.get_scan_state();
        assert(!scan_state.is_active);
        assert(scan_state.status_message == "Scan completed");
        
        std::cout << "PASSED\n";
    }
    
    static void test_state_store_events_integration() {
        std::cout << "Test: StateStore events integration... ";
        
        StateStore state_store;
        
        std::atomic<int> metadata_events{0};
        std::atomic<int> thumbnail_events{0};
        std::atomic<int> scan_events{0};
        
        // Subscribe to different event types
        state_store.get_event_bus().subscribe(StateEventType::IMAGE_METADATA_ADDED,
            [&metadata_events](const StateEvent&) { metadata_events++; });
        
        state_store.get_event_bus().subscribe(StateEventType::THUMBNAIL_READY,
            [&thumbnail_events](const StateEvent&) { thumbnail_events++; });
        
        state_store.get_event_bus().subscribe(StateEventType::SCAN_STARTED,
            [&scan_events](const StateEvent&) { scan_events++; });
        
        // Trigger events through StateStore operations
        StateImageInfo info;
        info.path = "/path/to/image.jpg";
        info.hash = "hash123";
        info.aspect_ratio = 1.5;
        state_store.add_image_metadata(info);
        
        std::vector<uint8_t> thumbnail_data = {1, 2, 3, 4, 5};
        state_store.add_thumbnail("hash123", 256, thumbnail_data, 256, 192);
        
        state_store.start_scan("/test/path");
        
        // Verify events were triggered
        assert(metadata_events.load() == 1);
        assert(thumbnail_events.load() == 1);
        assert(scan_events.load() == 1);
        
        std::cout << "PASSED\n";
    }
    
    static void test_cache_invalidation() {
        std::cout << "Test: Cache invalidation on image removal... ";
        
        StateStore state_store;
        
        std::atomic<int> invalidation_events{0};
        std::atomic<int> removal_events{0};
        
        // Subscribe to invalidation events
        state_store.get_event_bus().subscribe(StateEventType::THUMBNAIL_INVALIDATED,
            [&invalidation_events](const StateEvent&) { invalidation_events++; });
        
        state_store.get_event_bus().subscribe(StateEventType::IMAGE_REMOVED,
            [&removal_events](const StateEvent&) { removal_events++; });
        
        // Add image and thumbnails
        StateImageInfo info;
        info.path = "/path/to/image.jpg";
        info.hash = "hash123";
        info.aspect_ratio = 1.5;
        state_store.add_image_metadata(info);
        
        std::vector<uint8_t> thumb_data = {1, 2, 3, 4, 5};
        state_store.add_thumbnail("hash123", 128, thumb_data, 128, 96);
        state_store.add_thumbnail("hash123", 256, thumb_data, 256, 192);
        
        // Verify thumbnails exist
        assert(state_store.has_thumbnail("hash123", 128));
        assert(state_store.has_thumbnail("hash123", 256));
        assert(state_store.has_image("hash123"));
        
        // Remove image - should invalidate all thumbnails
        state_store.remove_image("hash123");
        
        // Verify image and thumbnails are gone
        assert(!state_store.has_image("hash123"));
        assert(!state_store.has_thumbnail("hash123", 128));
        assert(!state_store.has_thumbnail("hash123", 256));
        
        // Verify events were triggered
        assert(removal_events.load() == 1);
        assert(invalidation_events.load() == 2); // One for each thumbnail size
        
        std::cout << "PASSED\n";
    }
    
    static void test_cache_configuration() {
        std::cout << "Test: Cache configuration and limits... ";
        
        // Test with custom configuration
        CacheConfig config;
        config.max_memory_mb = 10; // 10MB
        config.max_items = 500;
        
        StateStore state_store(config);
        
        // Add some thumbnails
        StateImageInfo info;
        info.path = "/path/to/image.jpg";
        info.hash = "hash123";
        state_store.add_image_metadata(info);
        
        std::vector<uint8_t> large_thumb(1024 * 1024, 0xAB); // 1MB thumbnail
        state_store.add_thumbnail("hash123", 512, large_thumb, 512, 512);
        
        auto stats = state_store.get_cache_stats();
        assert(stats.thumbnail_count == 1);
        assert(stats.cache_memory_usage >= 1024 * 1024); // At least 1MB
        
        // Test configuration updates
        CacheConfig new_config;
        new_config.max_memory_mb = 5; // Reduce to 5MB
        new_config.max_items = 100;
        
        state_store.update_cache_config(new_config);
        
        // Cache should still work with new limits
        std::vector<uint8_t> small_thumb(1024, 0xCD); // 1KB thumbnail
        state_store.add_thumbnail("hash123", 64, small_thumb, 64, 64);
        
        stats = state_store.get_cache_stats();
        assert(stats.thumbnail_count >= 1); // Should have at least the small thumbnail
        
        std::cout << "PASSED\n";
    }
    
    static void test_concurrent_access() {
        std::cout << "Test: Concurrent access safety... ";
        
        StateStore state_store;
        std::atomic<int> completed_threads{0};
        const int num_threads = 4;
        const int operations_per_thread = 10;
        
        std::vector<std::thread> threads;
        
        // Start multiple threads doing concurrent operations
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&state_store, &completed_threads, t, operations_per_thread]() {
                for (int i = 0; i < operations_per_thread; ++i) {
                    // Add image metadata
                    StateImageInfo info;
                    info.path = "/path/image_" + std::to_string(t) + "_" + std::to_string(i) + ".jpg";
                    info.hash = "hash_" + std::to_string(t) + "_" + std::to_string(i);
                    info.aspect_ratio = 1.0 + (t * 0.1) + (i * 0.01);
                    state_store.add_image_metadata(info);
                    
                    // Add thumbnail
                    std::vector<uint8_t> thumb_data = {static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
                    state_store.add_thumbnail(info.hash, 256, thumb_data, 256, 256);
                    
                    // Read operations
                    state_store.has_image(info.hash);
                    state_store.get_image_count();
                    state_store.has_thumbnail(info.hash, 256);
                }
                completed_threads++;
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        assert(completed_threads.load() == num_threads);
        assert(state_store.get_image_count() == num_threads * operations_per_thread);
        
        std::cout << "PASSED\n";
    }
};

int main() {
    StateStoreTest::run_all_tests();
    return 0;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s