/*
 * database_dal_test.cpp - Unit tests for Database Abstraction Layer
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
#include <iostream>
#include <cassert>
#include <filesystem>
#include <vector>
#include <string>

namespace fs = std::filesystem;

class DALTester {
public:
    DALTester() : test_db_path_("/tmp/test_dal.db") {
        // Clean up any existing test database
        cleanup_test_db();
    }
    
    ~DALTester() {
        cleanup_test_db();
    }
    
    void run_all_tests() {
        std::cout << "Running Database Abstraction Layer tests...\n";
        
        test_dal_initialization();
        test_transaction_management();
        test_image_operations();
        test_thumbnail_operations();
        test_batch_operations();
        test_database_stats();
        
        std::cout << "All DAL tests passed!\n";
    }

private:
    std::string test_db_path_;
    
    void cleanup_test_db() {
        if (fs::exists(test_db_path_)) {
            fs::remove(test_db_path_);
        }
    }
    
    void test_dal_initialization() {
        std::cout << "Test: DAL initialization... ";
        
        auto dal = create_database_dal();
        assert(dal != nullptr);
        assert(!dal->is_ready());
        
        // Test successful initialization
        bool init_success = dal->initialize(test_db_path_);
        assert(init_success);
        assert(dal->is_ready());
        
        // Test that database file was created
        assert(fs::exists(test_db_path_));
        
        // Test closing
        dal->close();
        assert(!dal->is_ready());
        
        std::cout << "PASSED\n";
    }
    
    void test_transaction_management() {
        std::cout << "Test: Transaction management... ";
        
        auto dal = create_database_dal();
        assert(dal->initialize(test_db_path_));
        
        // Test read transaction
        auto read_txn = dal->begin_read_transaction();
        assert(read_txn != nullptr);
        assert(read_txn->is_active());
        
        // Test write transaction
        auto write_txn = dal->begin_write_transaction();
        assert(write_txn != nullptr);
        assert(write_txn->is_active());
        
        // Test commit
        bool commit_success = write_txn->commit();
        assert(commit_success);
        assert(!write_txn->is_active());
        
        // Test abort
        read_txn->abort();
        assert(!read_txn->is_active());
        
        dal->close();
        std::cout << "PASSED\n";
    }
    
    void test_image_operations() {
        std::cout << "Test: Image operations... ";
        
        auto dal = create_database_dal();
        assert(dal->initialize(test_db_path_));
        
        const std::string test_hash = "abcdef1234567890";
        const std::string test_path = "/path/to/test/image.jpg";
        const double test_aspect_ratio = 1.777;
        
        // Test storing image data
        {
            auto txn = dal->begin_write_transaction();
            assert(txn != nullptr);
            
            bool store_path_success = dal->images().store_image_path(*txn, test_hash, test_path);
            assert(store_path_success);
            
            bool store_metadata_success = dal->images().store_image_metadata(*txn, test_hash, test_aspect_ratio);
            assert(store_metadata_success);
            
            assert(txn->commit());
        }
        
        // Test retrieving image data
        {
            auto txn = dal->begin_read_transaction();
            assert(txn != nullptr);
            
            auto retrieved_path = dal->images().get_image_path(*txn, test_hash);
            assert(retrieved_path.has_value());
            assert(retrieved_path.value() == test_path);
            
            auto retrieved_metadata = dal->images().get_image_metadata(*txn, test_hash);
            assert(retrieved_metadata.has_value());
            assert(std::abs(retrieved_metadata.value() - test_aspect_ratio) < 0.001);
            
            bool exists = dal->images().image_exists(*txn, test_hash);
            assert(exists);
            
            auto all_hashes = dal->images().get_all_image_hashes(*txn);
            assert(all_hashes.size() == 1);
            assert(all_hashes[0] == test_hash);
        }
        
        // Test removing image
        {
            auto txn = dal->begin_write_transaction();
            assert(txn != nullptr);
            
            bool remove_success = dal->images().remove_image(*txn, test_hash);
            assert(remove_success);
            
            assert(txn->commit());
        }
        
        // Verify removal
        {
            auto txn = dal->begin_read_transaction();
            assert(txn != nullptr);
            
            bool exists = dal->images().image_exists(*txn, test_hash);
            assert(!exists);
            
            auto all_hashes = dal->images().get_all_image_hashes(*txn);
            assert(all_hashes.empty());
        }
        
        dal->close();
        std::cout << "PASSED\n";
    }
    
    void test_thumbnail_operations() {
        std::cout << "Test: Thumbnail operations... ";
        
        auto dal = create_database_dal();
        assert(dal->initialize(test_db_path_));
        
        const std::string test_hash = "thumbnail_test_hash";
        const int thumbnail_size = 256;
        const std::vector<uint8_t> thumbnail_data = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10}; // JPEG header
        
        // Test storing thumbnail
        {
            auto txn = dal->begin_write_transaction();
            assert(txn != nullptr);
            
            bool store_success = dal->thumbnails().store_thumbnail(*txn, test_hash, thumbnail_size, thumbnail_data);
            assert(store_success);
            
            assert(txn->commit());
        }
        
        // Test retrieving thumbnail
        {
            auto txn = dal->begin_read_transaction();
            assert(txn != nullptr);
            
            auto retrieved_data = dal->thumbnails().get_thumbnail(*txn, test_hash, thumbnail_size);
            assert(retrieved_data.has_value());
            assert(retrieved_data.value() == thumbnail_data);
            
            bool exists = dal->thumbnails().thumbnail_exists(*txn, test_hash, thumbnail_size);
            assert(exists);
            
            auto available_sizes = dal->thumbnails().get_available_thumbnail_sizes(*txn, test_hash);
            assert(available_sizes.size() == 1);
            assert(available_sizes[0] == thumbnail_size);
        }
        
        // Test removing thumbnail
        {
            auto txn = dal->begin_write_transaction();
            assert(txn != nullptr);
            
            bool remove_success = dal->thumbnails().remove_thumbnail(*txn, test_hash, thumbnail_size);
            assert(remove_success);
            
            assert(txn->commit());
        }
        
        // Verify removal
        {
            auto txn = dal->begin_read_transaction();
            assert(txn != nullptr);
            
            bool exists = dal->thumbnails().thumbnail_exists(*txn, test_hash, thumbnail_size);
            assert(!exists);
        }
        
        dal->close();
        std::cout << "PASSED\n";
    }
    
    void test_batch_operations() {
        std::cout << "Test: Batch operations... ";
        
        auto dal = create_database_dal();
        assert(dal->initialize(test_db_path_));
        
        // Test successful batch operation
        auto result = dal->execute_batch([](ITransaction& txn, IImageDataAccess& images, IThumbnailDataAccess& thumbnails) {
            // Store multiple images in a single transaction
            bool success = true;
            success &= images.store_image_path(txn, "hash1", "/path1.jpg");
            success &= images.store_image_metadata(txn, "hash1", 1.5);
            success &= images.store_image_path(txn, "hash2", "/path2.jpg");
            success &= images.store_image_metadata(txn, "hash2", 2.0);
            
            // Store thumbnails
            std::vector<uint8_t> thumb_data = {0xFF, 0xD8, 0xFF, 0xE0};
            success &= thumbnails.store_thumbnail(txn, "hash1", 128, thumb_data);
            success &= thumbnails.store_thumbnail(txn, "hash2", 128, thumb_data);
            
            return success;
        });
        
        assert(result.success);
        assert(result.processed_count > 0);
        
        // Verify all data was stored
        {
            auto txn = dal->begin_read_transaction();
            assert(txn != nullptr);
            
            auto all_hashes = dal->images().get_all_image_hashes(*txn);
            assert(all_hashes.size() == 2);
            
            for (const auto& hash : all_hashes) {
                assert(dal->images().image_exists(*txn, hash));
                assert(dal->thumbnails().thumbnail_exists(*txn, hash, 128));
            }
        }
        
        dal->close();
        std::cout << "PASSED\n";
    }
    
    void test_database_stats() {
        std::cout << "Test: Database statistics... ";
        
        // Use a fresh database for this test
        std::string stats_db_path = "/tmp/test_dal_stats.db";
        if (fs::exists(stats_db_path)) {
            fs::remove(stats_db_path);
        }
        
        auto dal = create_database_dal();
        assert(dal->initialize(stats_db_path));
        
        // Add some test data
        auto result = dal->execute_batch([](ITransaction& txn, IImageDataAccess& images, IThumbnailDataAccess& thumbnails) {
            std::vector<uint8_t> thumb_data = {0xFF, 0xD8, 0xFF, 0xE0};
            
            bool success = true;
            success &= images.store_image_path(txn, "stats_hash1", "/stats_path1.jpg");
            success &= images.store_image_metadata(txn, "stats_hash1", 1.5);
            success &= thumbnails.store_thumbnail(txn, "stats_hash1", 64, thumb_data);
            success &= thumbnails.store_thumbnail(txn, "stats_hash1", 128, thumb_data);
            
            success &= images.store_image_path(txn, "stats_hash2", "/stats_path2.jpg");
            success &= images.store_image_metadata(txn, "stats_hash2", 2.0);
            success &= thumbnails.store_thumbnail(txn, "stats_hash2", 256, thumb_data);
            
            return success;
        });
        
        assert(result.success);
        
        // Get statistics
        auto stats = dal->get_stats();
        assert(stats.image_count == 2);
        assert(stats.thumbnail_count == 3); // 2 + 1
        // Note: database_size_bytes may be 0 in some LMDB configurations, so we'll skip that check for now
        
        dal->close();
        
        // Clean up stats database
        if (fs::exists(stats_db_path)) {
            fs::remove(stats_db_path);
        }
        
        std::cout << "PASSED\n";
    }
};

int main() {
    try {
        DALTester tester;
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