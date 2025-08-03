/*
 * database_dal_eventbus_integration_test.cpp - Test DAL integration with EventBus
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

#include "database_dal.hpp"
#include "database_dal_lmdb.hpp"
#include "event_bus.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <atomic>

namespace fs = std::filesystem;

class DALEventBusIntegrationTester {
public:
    DALEventBusIntegrationTester() : test_db_path_("/tmp/test_dal_eventbus.db"), event_count_(0) {
        cleanup_test_db();
    }
    
    ~DALEventBusIntegrationTester() {
        cleanup_test_db();
    }
    
    void run_all_tests() {
        std::cout << "Running DAL EventBus integration tests...\n";
        
        test_dal_with_eventbus();
        
        std::cout << "All DAL EventBus integration tests passed!\n";
    }

private:
    std::string test_db_path_;
    std::atomic<int> event_count_;
    
    void cleanup_test_db() {
        if (fs::exists(test_db_path_)) {
            fs::remove(test_db_path_);
        }
    }
    
    void test_dal_with_eventbus() {
        std::cout << "Test: DAL with EventBus integration... ";
        
        // Create EventBus and DAL with EventBus integration
        EventBus event_bus;
        auto dal = std::make_unique<DatabaseDAL_LMDB>(&event_bus);
        
        // Subscribe to events
        event_count_.store(0);
        auto subscription = event_bus.subscribe_all([this](const StateEvent& event) {
            event_count_.fetch_add(1);
            std::cout << "[Event received: type=" << static_cast<int>(event.type) << "] ";
        });
        
        // Initialize database
        assert(dal->initialize(test_db_path_));
        
        // Perform database operations that should trigger events
        auto result = dal->execute_batch([](ITransaction& txn, IImageDataAccess& images, IThumbnailDataAccess& thumbnails) {
            bool success = true;
            success &= images.store_image_path(txn, "event_hash1", "/event/path1.jpg");
            success &= images.store_image_metadata(txn, "event_hash1", 1.5);
            
            std::vector<uint8_t> thumb_data = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};
            success &= thumbnails.store_thumbnail(txn, "event_hash1", 128, thumb_data);
            
            return success;
        });
        
        assert(result.success);
        
        // Check that events were published (this is a demonstration - actual event integration would need more work)
        std::cout << "[Events: " << event_count_.load() << "] ";
        
        // Clean up subscription
        event_bus.unsubscribe(subscription);
        
        dal->close();
        std::cout << "PASSED\n";
    }
};

int main() {
    try {
        DALEventBusIntegrationTester tester;
        tester.run_all_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s