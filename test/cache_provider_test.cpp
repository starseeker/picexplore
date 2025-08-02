/*
 * cache_provider_test.cpp - Unit tests for CacheProvider
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

#include "cache_provider.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <cassert>

// Simple test framework
class CacheProviderTest {
public:
    static void run_all_tests() {
        std::cout << "Running CacheProvider tests...\n";
        
        test_basic_operations();
        test_lru_eviction();
        test_memory_limits();
        test_item_limits();
        test_eviction_callback();
        test_statistics();
        test_concurrent_access();
        test_remove_if();
        test_thumbnail_data_specialization();
        
        std::cout << "All CacheProvider tests passed!\n";
    }
    
private:
    static void test_basic_operations() {
        std::cout << "Test: Basic cache operations... ";
        
        CacheProvider<std::string> cache;
        
        // Test initial state
        assert(cache.get_item_count() == 0);
        assert(cache.get_memory_usage() == 0);
        assert(!cache.contains("key1"));
        assert(cache.get("key1") == nullptr);
        
        // Test put and get
        assert(cache.put("key1", "value1", 10));
        assert(cache.get_item_count() == 1);
        assert(cache.contains("key1"));
        
        auto item = cache.get("key1");
        assert(item != nullptr);
        assert(item->data == "value1");
        assert(item->key == "key1");
        assert(item->size_bytes == 10);
        
        // Test update existing key
        assert(cache.put("key1", "new_value1", 15));
        assert(cache.get_item_count() == 1); // Still one item
        
        item = cache.get("key1");
        assert(item != nullptr);
        assert(item->data == "new_value1");
        assert(item->size_bytes == 15);
        
        // Test multiple items
        assert(cache.put("key2", "value2", 20));
        assert(cache.put("key3", "value3", 30));
        assert(cache.get_item_count() == 3);
        
        // Test remove
        assert(cache.remove("key2"));
        assert(cache.get_item_count() == 2);
        assert(!cache.contains("key2"));
        assert(cache.get("key2") == nullptr);
        assert(!cache.remove("key2")); // Already removed
        
        // Test clear
        cache.clear();
        assert(cache.get_item_count() == 0);
        assert(cache.get_memory_usage() == 0);
        
        std::cout << "PASSED\n";
    }
    
    static void test_lru_eviction() {
        std::cout << "Test: LRU eviction behavior... ";
        
        CacheProvider<std::string> cache(0, 3); // Max 3 items
        
        // Fill cache
        assert(cache.put("key1", "value1", 10));
        assert(cache.put("key2", "value2", 10));
        assert(cache.put("key3", "value3", 10));
        assert(cache.get_item_count() == 3);
        
        // Access key1 to make it most recently used
        cache.get("key1");
        
        // Add another item - should evict key2 (least recently used)
        assert(cache.put("key4", "value4", 10));
        assert(cache.get_item_count() == 3);
        assert(cache.contains("key1")); // Recently accessed
        assert(!cache.contains("key2")); // Should be evicted
        assert(cache.contains("key3"));
        assert(cache.contains("key4")); // Newly added
        
        // Access key3 to move it to front
        cache.get("key3");
        
        // Add another item - should evict key1 (now least recently used)
        assert(cache.put("key5", "value5", 10));
        assert(cache.get_item_count() == 3);
        assert(!cache.contains("key1")); // Should be evicted
        assert(cache.contains("key3")); // Recently accessed
        assert(cache.contains("key4"));
        assert(cache.contains("key5")); // Newly added
        
        std::cout << "PASSED\n";
    }
    
    static void test_memory_limits() {
        std::cout << "Test: Memory limit enforcement... ";
        
        CacheProvider<std::string> cache(50); // 50 bytes max
        
        // Add items within limit
        assert(cache.put("key1", "value1", 20));
        assert(cache.put("key2", "value2", 20));
        assert(cache.get_memory_usage() == 40);
        assert(cache.get_item_count() == 2);
        
        // Add item that would exceed limit - should evict oldest
        assert(cache.put("key3", "value3", 30));
        assert(cache.get_memory_usage() == 50); // 20 + 30
        assert(cache.get_item_count() == 2);
        assert(!cache.contains("key1")); // Should be evicted
        assert(cache.contains("key2"));
        assert(cache.contains("key3"));
        
        // Try to add item larger than total cache size
        assert(!cache.put("large", "large_value", 60)); // Should be rejected
        assert(cache.get_item_count() == 2); // No change
        
        // Reduce memory limit to 35 bytes - should evict one item (key2=20 bytes)
        // leaving only key3=30 bytes
        cache.set_max_memory(35);
        assert(cache.get_memory_usage() <= 35);
        assert(cache.get_item_count() == 1); // Should have evicted key2
        assert(!cache.contains("key2")); // Should be evicted
        assert(cache.contains("key3")); // Should remain
        
        std::cout << "PASSED\n";
    }
    
    static void test_item_limits() {
        std::cout << "Test: Item count limit enforcement... ";
        
        CacheProvider<std::string> cache(0, 2); // Max 2 items
        
        assert(cache.put("key1", "value1", 10));
        assert(cache.put("key2", "value2", 10));
        assert(cache.get_item_count() == 2);
        
        // Adding third item should evict oldest
        assert(cache.put("key3", "value3", 10));
        assert(cache.get_item_count() == 2);
        assert(!cache.contains("key1"));
        assert(cache.contains("key2"));
        assert(cache.contains("key3"));
        
        // Change limit to 1 - should evict oldest
        cache.set_max_items(1);
        assert(cache.get_item_count() == 1);
        assert(cache.contains("key3")); // Most recent should remain
        
        std::cout << "PASSED\n";
    }
    
    static void test_eviction_callback() {
        std::cout << "Test: Eviction callback functionality... ";
        
        std::vector<std::string> evicted_keys;
        std::vector<std::string> evicted_values;
        
        CacheProvider<std::string> cache(0, 2); // Max 2 items
        cache.set_eviction_callback([&](const std::string& key, const std::string& value) {
            evicted_keys.push_back(key);
            evicted_values.push_back(value);
        });
        
        assert(cache.put("key1", "value1", 10));
        assert(cache.put("key2", "value2", 10));
        assert(evicted_keys.empty()); // No evictions yet
        
        // Trigger eviction
        assert(cache.put("key3", "value3", 10));
        assert(evicted_keys.size() == 1);
        assert(evicted_keys[0] == "key1");
        assert(evicted_values[0] == "value1");
        
        // Test manual removal callback
        cache.remove("key2");
        assert(evicted_keys.size() == 2);
        assert(evicted_keys[1] == "key2");
        assert(evicted_values[1] == "value2");
        
        // Test clear callback
        evicted_keys.clear();
        evicted_values.clear();
        cache.put("key4", "value4", 10);
        cache.clear();
        assert(evicted_keys.size() == 2); // key3 and key4
        
        std::cout << "PASSED\n";
    }
    
    static void test_statistics() {
        std::cout << "Test: Cache statistics... ";
        
        CacheProvider<std::string> cache;
        
        auto stats = cache.get_stats();
        assert(stats.total_items == 0);
        assert(stats.hit_count == 0);
        assert(stats.miss_count == 0);
        assert(stats.hit_ratio == 0.0);
        
        // Add items and test hits/misses
        cache.put("key1", "value1", 10);
        cache.put("key2", "value2", 20);
        
        cache.get("key1"); // Hit
        cache.get("key1"); // Hit
        cache.get("key3"); // Miss
        cache.get("key2"); // Hit
        cache.get("key4"); // Miss
        
        stats = cache.get_stats();
        assert(stats.total_items == 2);
        assert(stats.total_memory_bytes == 30);
        assert(stats.hit_count == 3);
        assert(stats.miss_count == 2);
        assert(stats.hit_ratio == 0.6); // 3/(3+2)
        
        std::cout << "PASSED\n";
    }
    
    static void test_concurrent_access() {
        std::cout << "Test: Concurrent access safety... ";
        
        CacheProvider<std::string> cache;
        std::atomic<int> completed_threads{0};
        const int num_threads = 4;
        const int operations_per_thread = 100;
        
        std::vector<std::thread> threads;
        
        // Start multiple threads doing concurrent operations
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&cache, &completed_threads, t, operations_per_thread]() {
                for (int i = 0; i < operations_per_thread; ++i) {
                    std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                    std::string value = "value_" + std::to_string(t) + "_" + std::to_string(i);
                    
                    // Put operation
                    cache.put(key, value, value.size());
                    
                    // Get operation
                    auto item = cache.get(key);
                    assert(item != nullptr);
                    assert(item->data == value);
                    
                    // Contains check
                    assert(cache.contains(key));
                    
                    // Occasionally remove items
                    if (i % 10 == 0) {
                        cache.remove(key);
                    }
                }
                completed_threads++;
            });
        }
        
        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
        
        assert(completed_threads.load() == num_threads);
        
        // Verify cache is still functional
        cache.put("final_test", "final_value", 10);
        auto item = cache.get("final_test");
        assert(item != nullptr);
        assert(item->data == "final_value");
        
        std::cout << "PASSED\n";
    }
    
    static void test_remove_if() {
        std::cout << "Test: Conditional removal (remove_if)... ";
        
        CacheProvider<std::string> cache;
        
        // Add test data
        cache.put("user_1", "data1", 10);
        cache.put("user_2", "data2", 10);
        cache.put("admin_1", "admin_data1", 15);
        cache.put("user_3", "data3", 10);
        cache.put("admin_2", "admin_data2", 15);
        
        assert(cache.get_item_count() == 5);
        
        // Remove all keys starting with "user_"
        size_t removed = cache.remove_if([](const std::string& key, const std::string& value) {
            return key.substr(0, 5) == "user_";
        });
        
        assert(removed == 3);
        assert(cache.get_item_count() == 2);
        assert(!cache.contains("user_1"));
        assert(!cache.contains("user_2"));
        assert(!cache.contains("user_3"));
        assert(cache.contains("admin_1"));
        assert(cache.contains("admin_2"));
        
        // Remove items with data containing "admin_data1"
        removed = cache.remove_if([](const std::string& key, const std::string& value) {
            return value == "admin_data1";
        });
        
        assert(removed == 1);
        assert(cache.get_item_count() == 1);
        assert(!cache.contains("admin_1"));
        assert(cache.contains("admin_2"));
        
        std::cout << "PASSED\n";
    }
    
    static void test_thumbnail_data_specialization() {
        std::cout << "Test: Thumbnail data (vector<uint8_t>) specialization... ";
        
        CacheProvider<std::vector<uint8_t>> cache;
        
        // Test with thumbnail-like data
        std::vector<uint8_t> thumbnail1 = {0xFF, 0xD8, 0xFF, 0xE0}; // JPEG header-like
        std::vector<uint8_t> thumbnail2(1024, 0xAB); // 1KB thumbnail
        
        // Test automatic size calculation
        assert(cache.put("thumb1", thumbnail1));
        assert(cache.put("thumb2", thumbnail2));
        
        auto item1 = cache.get("thumb1");
        assert(item1 != nullptr);
        assert(item1->data == thumbnail1);
        // Size should be data size + vector overhead
        assert(item1->size_bytes == thumbnail1.size() + sizeof(std::vector<uint8_t>));
        
        auto item2 = cache.get("thumb2");
        assert(item2 != nullptr);
        assert(item2->data == thumbnail2);
        assert(item2->size_bytes == thumbnail2.size() + sizeof(std::vector<uint8_t>));
        
        // Test with explicit size override
        std::vector<uint8_t> thumbnail3 = {1, 2, 3, 4};
        assert(cache.put("thumb3", thumbnail3, 500)); // Override size
        
        auto item3 = cache.get("thumb3");
        assert(item3 != nullptr);
        assert(item3->size_bytes == 500); // Should use overridden size
        
        std::cout << "PASSED\n";
    }
};

int main() {
    CacheProviderTest::run_all_tests();
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