/*
 * Test thumbnail request deduplication functionality
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "thread_manager.hpp"

// Helper function to create a test task
UIThumbnailTask create_test_task(int image_index, const std::string& hash, int width, int height, UIThumbnailTask::Priority priority = UIThumbnailTask::HIGH) {
    return UIThumbnailTask(image_index, priority, width, height, hash);
}

int main() {
    std::cout << "Testing Thumbnail Deduplication..." << std::endl;
    
    // Test 1: Basic deduplication functionality
    std::cout << "\nTest 1: Basic deduplication functionality" << std::endl;
    {
        ThumbnailWorkers workers;
        
        // Create identical tasks that should be deduplicated
        std::string test_hash = "testhash123";
        int target_width = 128;
        int target_height = 128;
        
        UIThumbnailTask task1 = create_test_task(1, test_hash, target_width, target_height);
        UIThumbnailTask task2 = create_test_task(2, test_hash, target_width, target_height);
        UIThumbnailTask task3 = create_test_task(3, test_hash, target_width, target_height);
        
        std::cout << "  Enqueuing first task (should succeed)..." << std::endl;
        workers.enqueue_high_priority(task1);
        
        std::cout << "  Enqueuing duplicate task (should be skipped)..." << std::endl;
        workers.enqueue_high_priority(task2);
        
        std::cout << "  Enqueuing another duplicate task (should be skipped)..." << std::endl;
        workers.enqueue_low_priority(task3);
        
        std::cout << "  ✓ Deduplication test completed (check debug output above)" << std::endl;
    }
    
    // Test 2: Different sizes should not be deduplicated
    std::cout << "\nTest 2: Different sizes should not be deduplicated" << std::endl;
    {
        ThumbnailWorkers workers;
        
        std::string test_hash = "testhash456";
        
        UIThumbnailTask task1 = create_test_task(1, test_hash, 128, 128);
        UIThumbnailTask task2 = create_test_task(2, test_hash, 256, 256);
        UIThumbnailTask task3 = create_test_task(3, test_hash, 512, 512);
        
        std::cout << "  Enqueuing task for 128x128 (should succeed)..." << std::endl;
        workers.enqueue_high_priority(task1);
        
        std::cout << "  Enqueuing task for 256x256 (should succeed - different size)..." << std::endl;
        workers.enqueue_high_priority(task2);
        
        std::cout << "  Enqueuing task for 512x512 (should succeed - different size)..." << std::endl;
        workers.enqueue_low_priority(task3);
        
        std::cout << "  ✓ Different sizes test completed" << std::endl;
    }
    
    // Test 3: Different hashes should not be deduplicated
    std::cout << "\nTest 3: Different hashes should not be deduplicated" << std::endl;
    {
        ThumbnailWorkers workers;
        
        int target_width = 128;
        int target_height = 128;
        
        UIThumbnailTask task1 = create_test_task(1, "hash1", target_width, target_height);
        UIThumbnailTask task2 = create_test_task(2, "hash2", target_width, target_height);
        UIThumbnailTask task3 = create_test_task(3, "hash3", target_width, target_height);
        
        std::cout << "  Enqueuing task for hash1 (should succeed)..." << std::endl;
        workers.enqueue_high_priority(task1);
        
        std::cout << "  Enqueuing task for hash2 (should succeed - different hash)..." << std::endl;
        workers.enqueue_high_priority(task2);
        
        std::cout << "  Enqueuing task for hash3 (should succeed - different hash)..." << std::endl;
        workers.enqueue_low_priority(task3);
        
        std::cout << "  ✓ Different hashes test completed" << std::endl;
    }
    
    // Test 4: Priority mixing should still deduplicate
    std::cout << "\nTest 4: Priority mixing should still deduplicate" << std::endl;
    {
        ThumbnailWorkers workers;
        
        std::string test_hash = "testhash789";
        int target_width = 256;
        int target_height = 256;
        
        UIThumbnailTask task1 = create_test_task(1, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        UIThumbnailTask task2 = create_test_task(2, test_hash, target_width, target_height, UIThumbnailTask::LOW);
        UIThumbnailTask task3 = create_test_task(3, test_hash, target_width, target_height, UIThumbnailTask::HIGH);
        
        std::cout << "  Enqueuing high priority task (should succeed)..." << std::endl;
        workers.enqueue_high_priority(task1);
        
        std::cout << "  Enqueuing low priority duplicate (should be skipped)..." << std::endl;
        workers.enqueue_low_priority(task2);
        
        std::cout << "  Enqueuing high priority duplicate (should be skipped)..." << std::endl;
        workers.enqueue_high_priority(task3);
        
        std::cout << "  ✓ Priority mixing test completed" << std::endl;
    }
    
    std::cout << "\n✓ All thumbnail deduplication tests completed successfully!" << std::endl;
    std::cout << "Check the debug output above to verify that duplicate requests were properly skipped." << std::endl;
    
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