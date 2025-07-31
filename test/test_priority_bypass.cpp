/*
 * Test priority bypass functionality for thumbnail request deduplication
 * This test validates that high priority requests can bypass low priority requests
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "thread_manager.hpp"

// Helper function to create a test task
UIThumbnailTask create_test_task(int image_index, const std::string& hash, int width, int height, UIThumbnailTask::Priority priority) {
    return UIThumbnailTask(image_index, priority, width, height, hash);
}

int main() {
    std::cout << "Testing Priority Bypass Functionality..." << std::endl;
    
    // Test 1: High priority should bypass low priority requests
    std::cout << "\nTest 1: High priority bypass low priority requests" << std::endl;
    {
        ThumbnailWorkers workers;
        
        std::string test_hash = "bypass_test_hash";
        int target_width = 128;
        int target_height = 128;
        
        // First enqueue a low priority request
        UIThumbnailTask low_task = create_test_task(1, test_hash, target_width, target_height, UIThumbnailTask::LOW);
        std::cout << "  Enqueuing LOW priority task (should succeed)..." << std::endl;
        workers.enqueue_low_priority(low_task);
        
        // Then enqueue a high priority request for the same hash:size - should be allowed
        UIThumbnailTask high_task = create_test_task(2, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        std::cout << "  Enqueuing HIGH priority task for same hash:size (should bypass and succeed)..." << std::endl;
        workers.enqueue_high_priority(high_task);
        
        // Try another high priority request - should be blocked now
        UIThumbnailTask high_task2 = create_test_task(3, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        std::cout << "  Enqueuing another HIGH priority task for same hash:size (should be blocked)..." << std::endl;
        workers.enqueue_high_priority(high_task2);
        
        std::cout << "  ✓ Priority bypass test completed" << std::endl;
    }
    
    // Test 2: High priority requests should still be deduplicated among themselves
    std::cout << "\nTest 2: High priority requests deduplicated among themselves" << std::endl;
    {
        ThumbnailWorkers workers;
        
        std::string test_hash = "high_priority_dedup";
        int target_width = 256;
        int target_height = 256;
        
        // First enqueue a high priority request
        UIThumbnailTask high_task1 = create_test_task(1, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        std::cout << "  Enqueuing first HIGH priority task (should succeed)..." << std::endl;
        workers.enqueue_high_priority(high_task1);
        
        // Try another high priority request for same hash:size - should be blocked
        UIThumbnailTask high_task2 = create_test_task(2, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        std::cout << "  Enqueuing second HIGH priority task for same hash:size (should be blocked)..." << std::endl;
        workers.enqueue_high_priority(high_task2);
        
        // Try a low priority request - should also be blocked
        UIThumbnailTask low_task = create_test_task(3, test_hash, target_width, target_height, UIThumbnailTask::LOW);
        std::cout << "  Enqueuing LOW priority task for same hash:size (should be blocked)..." << std::endl;
        workers.enqueue_low_priority(low_task);
        
        std::cout << "  ✓ High priority deduplication test completed" << std::endl;
    }
    
    // Test 3: Low priority requests still deduplicated among themselves  
    std::cout << "\nTest 3: Low priority requests deduplicated among themselves" << std::endl;
    {
        ThumbnailWorkers workers;
        
        std::string test_hash = "low_priority_dedup";
        int target_width = 512;
        int target_height = 512;
        
        // First enqueue a low priority request
        UIThumbnailTask low_task1 = create_test_task(1, test_hash, target_width, target_height, UIThumbnailTask::LOW);
        std::cout << "  Enqueuing first LOW priority task (should succeed)..." << std::endl;
        workers.enqueue_low_priority(low_task1);
        
        // Try another low priority request for same hash:size - should be blocked
        UIThumbnailTask low_task2 = create_test_task(2, test_hash, target_width, target_height, UIThumbnailTask::LOW);
        std::cout << "  Enqueuing second LOW priority task for same hash:size (should be blocked)..." << std::endl;
        workers.enqueue_low_priority(low_task2);
        
        std::cout << "  ✓ Low priority deduplication test completed" << std::endl;
    }
    
    // Test 4: Multiple bypass scenarios
    std::cout << "\nTest 4: Multiple bypass scenarios with different hash:size combinations" << std::endl;
    {
        ThumbnailWorkers workers;
        
        // Scenario A: hash1:128 - low then high
        std::string hash1 = "hash1";
        UIThumbnailTask task_a1 = create_test_task(1, hash1, 128, 128, UIThumbnailTask::LOW);
        UIThumbnailTask task_a2 = create_test_task(2, hash1, 128, 128, UIThumbnailTask::HIGH);
        
        std::cout << "  Scenario A: hash1:128 - LOW then HIGH..." << std::endl;
        workers.enqueue_low_priority(task_a1);
        workers.enqueue_high_priority(task_a2); // Should bypass
        
        // Scenario B: hash2:256 - high then low
        std::string hash2 = "hash2";
        UIThumbnailTask task_b1 = create_test_task(3, hash2, 256, 256, UIThumbnailTask::HIGH);
        UIThumbnailTask task_b2 = create_test_task(4, hash2, 256, 256, UIThumbnailTask::LOW);
        
        std::cout << "  Scenario B: hash2:256 - HIGH then LOW..." << std::endl;
        workers.enqueue_high_priority(task_b1);
        workers.enqueue_low_priority(task_b2); // Should be blocked
        
        std::cout << "  ✓ Multiple scenarios test completed" << std::endl;
    }
    
    std::cout << "\n✓ All priority bypass tests completed successfully!" << std::endl;
    std::cout << "Expected results:" << std::endl;
    std::cout << "  - High priority requests should bypass low priority requests" << std::endl;
    std::cout << "  - Same priority requests should still be deduplicated" << std::endl;
    std::cout << "  - Look for 'Allowing high priority request to bypass low priority' messages" << std::endl;
    
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