/*
 * mvc_components_test.cpp - Tests for MVC architecture components
 *
 * Copyright (c) 2025 Clifford Yapp
 */

#include <iostream>
#include <cassert>
#include <memory>
#include <chrono>
#include <thread>

#include "controllers.hpp"
#include "state_store.hpp"

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

// Test ApplicationController basic functionality
void test_application_controller_creation() {
    auto controller = std::make_shared<ApplicationController>();
    bool initialized = controller->initialize();
    
    TestLogger::log("ApplicationController creation and initialization", initialized);
    
    // Test that sub-controllers are available
    bool has_scan_controller = (controller->get_scan_controller() != nullptr);
    bool has_gallery_controller = (controller->get_gallery_controller() != nullptr);
    bool has_state_store = (controller->get_state_store() != nullptr);
    
    TestLogger::log("ApplicationController has required sub-components", 
                   has_scan_controller && has_gallery_controller && has_state_store);
    
    controller->shutdown();
}

// Test ScanController functionality
void test_scan_controller() {
    auto state_store = std::make_shared<StateStore>();
    auto thread_manager = std::make_shared<ThreadManager>();
    auto scan_controller = std::make_shared<ScanController>(state_store, thread_manager);
    
    bool initialized = scan_controller->initialize();
    TestLogger::log("ScanController initialization", initialized);
    
    // Test initial state
    bool not_scanning_initially = !scan_controller->is_scanning();
    TestLogger::log("ScanController not scanning initially", not_scanning_initially);
    
    // Test scan state retrieval
    ScanState initial_state = scan_controller->get_scan_state();
    bool initial_state_correct = !initial_state.is_active;
    TestLogger::log("ScanController initial scan state", initial_state_correct);
    
    scan_controller->shutdown();
}

// Test GalleryController functionality  
void test_gallery_controller() {
    auto state_store = std::make_shared<StateStore>();
    auto thread_manager = std::make_shared<ThreadManager>();
    auto gallery_controller = std::make_shared<GalleryController>(state_store, thread_manager);
    
    bool initialized = gallery_controller->initialize();
    TestLogger::log("GalleryController initialization", initialized);
    
    // Test initial state
    bool no_images_initially = (gallery_controller->get_image_count() == 0);
    bool no_selection_initially = (gallery_controller->get_selected_image_index() == -1);
    TestLogger::log("GalleryController initial state", no_images_initially && no_selection_initially);
    
    // Test display configuration
    GalleryDisplayConfig config;
    config.row_height = 200;
    config.spacing_horizontal = 15;
    gallery_controller->update_display_config(config);
    
    GalleryDisplayConfig retrieved_config = gallery_controller->get_display_config();
    bool config_updated = (retrieved_config.row_height == 200 && 
                          retrieved_config.spacing_horizontal == 15);
    TestLogger::log("GalleryController display configuration", config_updated);
    
    gallery_controller->shutdown();
}

// Test interaction between controllers and state store
void test_mvc_integration() {
    auto state_store = std::make_shared<StateStore>();
    
    // Add some test image data to state store
    StateImageInfo test_image;
    test_image.path = "/test/image1.jpg";
    test_image.hash = "testhash123";
    test_image.aspect_ratio = 1.5;
    
    state_store->add_image_metadata(test_image);
    
    // Create gallery controller and test data retrieval
    auto thread_manager = std::make_shared<ThreadManager>();
    auto gallery_controller = std::make_shared<GalleryController>(state_store, thread_manager);
    gallery_controller->initialize();
    
    // Load images from state store
    gallery_controller->load_images();
    
    bool has_image = (gallery_controller->get_image_count() == 1);
    TestLogger::log("MVC integration - image data flow", has_image);
    
    if (has_image) {
        auto image_state = gallery_controller->get_image_state(0);
        bool correct_data = (image_state != nullptr && 
                           image_state->metadata.path == "/test/image1.jpg" &&
                           image_state->metadata.hash == "testhash123");
        TestLogger::log("MVC integration - correct image data", correct_data);
    }
    
    gallery_controller->shutdown();
}

// Test event-driven communication
void test_event_communication() {
    auto state_store = std::make_shared<StateStore>();
    
    bool event_received = false;
    std::string received_hash;
    
    // Subscribe to image metadata events
    auto subscription_id = state_store->get_event_bus().subscribe(
        StateEventType::IMAGE_METADATA_ADDED,
        [&](const StateEvent& event) {
            const ImageEvent* img_event = static_cast<const ImageEvent*>(&event);
            event_received = true;
            received_hash = img_event->hash;
        }
    );
    
    // Add image metadata to trigger event
    StateImageInfo test_image;
    test_image.path = "/test/event_test.jpg";
    test_image.hash = "eventhash456";
    test_image.aspect_ratio = 2.0;
    
    state_store->add_image_metadata(test_image);
    
    // Give some time for event processing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    bool event_correct = (event_received && received_hash == "eventhash456");
    TestLogger::log("Event-driven communication", event_correct);
    
    state_store->get_event_bus().unsubscribe(subscription_id);
}

// Test controller lifecycle and cleanup
void test_controller_lifecycle() {
    auto app_controller = std::make_shared<ApplicationController>();
    
    bool init_success = app_controller->initialize();
    TestLogger::log("Controller lifecycle - initialization", init_success);
    
    // Test that controllers are properly initialized
    auto scan_controller = app_controller->get_scan_controller();
    auto gallery_controller = app_controller->get_gallery_controller();
    
    bool controllers_available = (scan_controller != nullptr && gallery_controller != nullptr);
    TestLogger::log("Controller lifecycle - sub-controllers available", controllers_available);
    
    // Test shutdown
    app_controller->shutdown();
    
    // After shutdown, scanning should not be active
    bool not_scanning_after_shutdown = !scan_controller->is_scanning();
    TestLogger::log("Controller lifecycle - proper shutdown", not_scanning_after_shutdown);
}

int main() {
    std::cout << "Running MVC Components Tests...\n" << std::endl;
    
    test_application_controller_creation();
    test_scan_controller();
    test_gallery_controller();
    test_mvc_integration();
    test_event_communication();
    test_controller_lifecycle();
    
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