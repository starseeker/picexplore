/*
 * database_manager_dal_integration_test.cpp - Integration test for DatabaseManager with DAL
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

#include "database.hpp"
#include "database_dal.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

class DatabaseManagerDALTester {
public:
    DatabaseManagerDALTester() : test_db_path_("/tmp/test_db_manager_dal.db") {
        cleanup_test_db();
    }
    
    ~DatabaseManagerDALTester() {
        cleanup_test_db();
    }
    
    void run_all_tests() {
        std::cout << "Running DatabaseManager DAL integration tests...\n";
        
        test_basic_database_operations();
        test_dal_access();
        test_image_operations_integration();
        
        std::cout << "All DatabaseManager DAL integration tests passed!\n";
    }

private:
    std::string test_db_path_;
    
    void cleanup_test_db() {
        if (fs::exists(test_db_path_)) {
            fs::remove(test_db_path_);
        }
    }
    
    void test_basic_database_operations() {
        std::cout << "Test: Basic database operations with DAL... ";
        
        DatabaseManager db_manager;
        
        // Test opening database
        bool open_success = db_manager.open(test_db_path_);
        assert(open_success);
        
        // Test that database file was created
        assert(fs::exists(test_db_path_));
        
        // Test closing
        db_manager.close();
        
        std::cout << "PASSED\n";
    }
    
    void test_dal_access() {
        std::cout << "Test: DAL access through DatabaseManager... ";
        
        DatabaseManager db_manager;
        assert(db_manager.open(test_db_path_));
        
        // Test that we can access the DAL
        auto* dal = db_manager.get_dal();
        assert(dal != nullptr);
        assert(dal->is_ready());
        
        // Test that we can create transactions
        auto read_txn = dal->begin_read_transaction();
        assert(read_txn != nullptr);
        assert(read_txn->is_active());
        
        auto write_txn = dal->begin_write_transaction();
        assert(write_txn != nullptr);
        assert(write_txn->is_active());
        
        // Test committing a transaction
        assert(write_txn->commit());
        assert(!write_txn->is_active());
        
        db_manager.close();
        std::cout << "PASSED\n";
    }
    
    void test_image_operations_integration() {
        std::cout << "Test: Image operations integration with DAL... ";
        
        // Use a different database for this test
        std::string integration_db_path = "/tmp/test_db_manager_dal_integration.db";
        if (fs::exists(integration_db_path)) {
            fs::remove(integration_db_path);
        }
        
        DatabaseManager db_manager;
        assert(db_manager.open(integration_db_path));
        
        // Add some test data through DAL
        auto* dal = db_manager.get_dal();
        auto result = dal->execute_batch([](ITransaction& txn, IImageDataAccess& images, IThumbnailDataAccess& thumbnails) {
            bool success = true;
            success &= images.store_image_path(txn, "test_hash1", "/test/path1.jpg");
            success &= images.store_image_metadata(txn, "test_hash1", 1.5);
            
            success &= images.store_image_path(txn, "test_hash2", "/test/path2.jpg");
            success &= images.store_image_metadata(txn, "test_hash2", 2.0);
            
            return success;
        });
        
        assert(result.success);
        
        // Test has_thumbnails method (this uses DAL directly and shouldn't have thumbnails)
        assert(!db_manager.has_thumbnails("test_hash1"));
        assert(!db_manager.has_thumbnails("test_hash2"));
        assert(!db_manager.has_thumbnails("nonexistent_hash"));
        
        // Test get_all_images method - just check the count for now
        auto all_images = db_manager.get_all_images();
        std::cout << "[Got " << all_images.size() << " images] ";
        assert(all_images.size() == 2);
        
        db_manager.close();
        
        // Clean up
        if (fs::exists(integration_db_path)) {
            fs::remove(integration_db_path);
        }
        
        std::cout << "PASSED\n";
    }
};

int main() {
    try {
        DatabaseManagerDALTester tester;
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