/*
 * Manual verification test for thumbnail deduplication
 * This test simulates a realistic scenario with actual worker threads
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include "thread_manager.hpp"

int main() {
    std::cout << "Manual Verification: Thumbnail Deduplication with Worker Threads" << std::endl;
    std::cout << "=================================================================" << std::endl;
    
    // Initialize a full ThreadManager to test realistic conditions
    ThreadManager manager;
    
    // Set up progress callbacks to observe behavior
    manager.set_progress_callback([](int current, int total, const std::string& status) {
        std::cout << "[PROGRESS] " << current << "/" << total << " - " << status << std::endl;
    });
    
    std::cout << "\nTest: Multiple requests for the same thumbnail should be deduplicated" << std::endl;
    std::cout << "Expected: Only one request should be processed per unique hash:size combination\n" << std::endl;
    
    // Create multiple requests for the same thumbnail
    std::string test_hash = "simulation_hash_12345";
    int target_width = 256;
    int target_height = 256;
    
    std::cout << "Submitting 5 identical thumbnail requests rapidly..." << std::endl;
    
    // Submit multiple identical requests rapidly
    for (int i = 0; i < 5; i++) {
        UIThumbnailTask task(i, UIThumbnailTask::HIGH, target_width, target_height, test_hash);
        manager.request_thumbnail(task);
        std::cout << "  Submitted request " << (i+1) << " for hash: " << test_hash << std::endl;
        
        // Small delay to simulate realistic timing
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "\nSubmitting requests for different sizes (should NOT be deduplicated)..." << std::endl;
    
    // Submit requests for different sizes - these should not be deduplicated
    std::vector<std::pair<int,int>> different_sizes = {{128, 128}, {512, 512}, {1024, 1024}};
    for (auto& size : different_sizes) {
        UIThumbnailTask task(10 + (&size - &different_sizes[0]), UIThumbnailTask::LOW, size.first, size.second, test_hash);
        manager.request_thumbnail(task);
        std::cout << "  Submitted request for " << size.first << "x" << size.second << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "\nSubmitting requests for different hashes (should NOT be deduplicated)..." << std::endl;
    
    // Submit requests for different hashes - these should not be deduplicated
    std::vector<std::string> different_hashes = {"hash_a", "hash_b", "hash_c"};
    for (auto& hash : different_hashes) {
        UIThumbnailTask task(20 + (&hash - &different_hashes[0]), UIThumbnailTask::HIGH, target_width, target_height, hash);
        manager.request_thumbnail(task);
        std::cout << "  Submitted request for hash: " << hash << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    std::cout << "\nWaiting for processing to complete..." << std::endl;
    
    // Give worker threads time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Try to get any results (will be empty since we don't have a real database, but tests the queue)
    UIDrawTask result;
    int result_count = 0;
    while (manager.get_thumbnail_result(result)) {
        result_count++;
        std::cout << "  Got result for image_index: " << result.image_index << ", cache_key: " << result.cache_key << std::endl;
    }
    
    std::cout << "\nResults received: " << result_count << std::endl;
    std::cout << "\nVerification Instructions:" << std::endl;
    std::cout << "1. Check the debug output above for 'Skipping duplicate' messages" << std::endl;
    std::cout << "2. Verify that identical hash:size combinations are properly deduplicated" << std::endl;
    std::cout << "3. Verify that different sizes and hashes are allowed through" << std::endl;
    std::cout << "4. Look for 'Marked request in-flight' and 'Marked request completed' messages" << std::endl;
    
    std::cout << "\nShutting down gracefully..." << std::endl;
    manager.shutdown_all();
    
    std::cout << "\n✓ Manual verification completed!" << std::endl;
    std::cout << "The deduplication mechanism is working correctly if you see:" << std::endl;
    std::cout << "  - 'Skipping duplicate' messages for identical requests" << std::endl;
    std::cout << "  - Successful enqueuing for different sizes/hashes" << std::endl;
    std::cout << "  - Proper cleanup during shutdown" << std::endl;
    
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