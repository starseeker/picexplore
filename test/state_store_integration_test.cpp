/*
 * state_store_integration_test.cpp - Integration test for StateStore without FLTK
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include "state_store.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

// Mock ThreadManager-like integration
class MockThreadManager {
public:
    MockThreadManager() : state_store_(std::make_shared<StateStore>()) {}
    
    StateStore& get_state_store() { return *state_store_; }
    const StateStore& get_state_store() const { return *state_store_; }
    
    // Simulate metadata callback from scanning thread
    void simulate_image_discovery(const StateImageInfo& info) {
        state_store_->add_image_metadata(info);
    }
    
    // Simulate thumbnail generation
    void simulate_thumbnail_generation(const std::string& hash, int size, 
                                     const std::vector<uint8_t>& data) {
        state_store_->add_thumbnail(hash, size, data, size, size);
    }
    
private:
    std::shared_ptr<StateStore> state_store_;
};

int main() {
    std::cout << "Running StateStore integration test (without FLTK dependencies)...\n";
    
    // Create mock ThreadManager
    MockThreadManager mock_thread_manager;
    
    // Test StateStore accessibility
    auto& state_store = mock_thread_manager.get_state_store();
    
    std::cout << "Test: StateStore access through mock ThreadManager... ";
    
    // Test basic StateStore operations
    StateImageInfo test_image;
    test_image.path = "/test/image.jpg";
    test_image.hash = "testhash123";
    test_image.aspect_ratio = 1.5;
    
    mock_thread_manager.simulate_image_discovery(test_image);
    
    // Verify image was added
    if (state_store.get_image_count() == 1 && state_store.has_image("testhash123")) {
        std::cout << "PASSED\n";
    } else {
        std::cout << "FAILED\n";
        return 1;
    }
    
    // Test event subscription (UI-like component)
    std::cout << "Test: Event bus integration with UI-like subscriber... ";
    
    std::atomic<int> metadata_events{0};
    std::atomic<int> thumbnail_events{0};
    std::atomic<int> scan_events{0};
    
    // Subscribe to events (like UI would)
    auto metadata_sub = state_store.get_event_bus().subscribe(StateEventType::IMAGE_METADATA_ADDED,
        [&metadata_events](const StateEvent& event) {
            metadata_events++;
        });
    
    auto thumbnail_sub = state_store.get_event_bus().subscribe(StateEventType::THUMBNAIL_READY,
        [&thumbnail_events](const StateEvent& event) {
            thumbnail_events++;
        });
    
    auto scan_sub = state_store.get_event_bus().subscribe(StateEventType::SCAN_STARTED,
        [&scan_events](const StateEvent& event) {
            scan_events++;
        });
    
    // Simulate ThreadManager operations
    StateImageInfo test_image2;
    test_image2.path = "/test/image2.jpg";
    test_image2.hash = "testhash456";
    test_image2.aspect_ratio = 2.0;
    
    mock_thread_manager.simulate_image_discovery(test_image2);
    
    // Simulate thumbnail generation
    std::vector<uint8_t> thumbnail_data = {1, 2, 3, 4, 5};
    mock_thread_manager.simulate_thumbnail_generation("testhash123", 256, thumbnail_data);
    
    // Simulate scan start
    state_store.start_scan("/test/directory");
    
    // Give a moment for event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    if (metadata_events.load() == 1 && thumbnail_events.load() == 1 && scan_events.load() == 1) {
        std::cout << "PASSED\n";
    } else {
        std::cout << "FAILED (metadata: " << metadata_events.load() 
                  << ", thumbnail: " << thumbnail_events.load()
                  << ", scan: " << scan_events.load() << ")\n";
        return 1;
    }
    
    // Test scan state management  
    std::cout << "Test: Scan state management... ";
    
    auto scan_state = state_store.get_scan_state();
    
    if (scan_state.is_active && scan_state.directory_path == "/test/directory") {
        std::cout << "PASSED\n";
    } else {
        std::cout << "FAILED\n";
        return 1;
    }
    
    // Test StateStore query operations
    std::cout << "Test: StateStore query operations... ";
    
    // Should have 2 images now
    if (state_store.get_image_count() == 2) {
        // Test thumbnail availability
        if (state_store.has_thumbnail("testhash123", 256) && 
            !state_store.has_thumbnail("testhash456", 256)) {
            std::cout << "PASSED\n";
        } else {
            std::cout << "FAILED (thumbnail availability check)\n";
            return 1;
        }
    } else {
        std::cout << "FAILED (expected 2 images, got " << state_store.get_image_count() << ")\n";
        return 1;
    }
    
    // Cleanup
    state_store.get_event_bus().unsubscribe(metadata_sub);
    state_store.get_event_bus().unsubscribe(thumbnail_sub);
    state_store.get_event_bus().unsubscribe(scan_sub);
    
    std::cout << "All StateStore integration tests passed!\n";
    std::cout << "StateStore successfully provides centralized state management with event notifications.\n";
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