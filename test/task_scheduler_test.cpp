/*
 * task_scheduler_test.cpp - Unit tests for TaskScheduler
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

#include "task_scheduler.hpp"
#include <iostream>
#include <chrono>
#include <atomic>
#include <cassert>

// Simple test framework
class TaskSchedulerTest {
public:
    static void run_all_tests() {
        std::cout << "Running TaskScheduler tests...\n";
        
        test_basic_task_execution();
        test_multiple_tasks();
        test_shutdown_and_cleanup();
        test_concurrent_task_submission();
        
        std::cout << "All tests passed!\n";
    }
    
private:
    static void test_basic_task_execution() {
        std::cout << "Test: Basic task execution... ";
        
        TaskScheduler scheduler;
        std::atomic<bool> task_executed{false};
        
        // Start with 2 threads
        assert(scheduler.start(2, "TestWorker"));
        assert(scheduler.is_running());
        assert(scheduler.get_thread_count() == 2);
        
        // Submit a simple task
        bool submitted = scheduler.submit_task([&task_executed]() {
            task_executed.store(true);
        });
        assert(submitted);
        
        // Wait for task to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(task_executed.load());
        
        scheduler.shutdown();
        scheduler.join_all();
        assert(!scheduler.is_running());
        
        std::cout << "PASSED\n";
    }
    
    static void test_multiple_tasks() {
        std::cout << "Test: Multiple tasks execution... ";
        
        TaskScheduler scheduler;
        std::atomic<int> task_count{0};
        const int num_tasks = 10;
        
        assert(scheduler.start(3, "MultiTest"));
        
        // Submit multiple tasks
        for (int i = 0; i < num_tasks; ++i) {
            bool submitted = scheduler.submit_task([&task_count]() {
                task_count.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
            assert(submitted);
        }
        
        // Wait for all tasks to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        assert(task_count.load() == num_tasks);
        
        scheduler.shutdown();
        scheduler.join_all();
        
        std::cout << "PASSED\n";
    }
    
    static void test_shutdown_and_cleanup() {
        std::cout << "Test: Shutdown and cleanup... ";
        
        TaskScheduler scheduler;
        assert(scheduler.start(2, "ShutdownTest"));
        
        // Submit a task that will be cancelled
        std::atomic<bool> long_task_started{false};
        std::atomic<bool> long_task_completed{false};
        
        scheduler.submit_task([&]() {
            long_task_started.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            long_task_completed.store(true);
        });
        
        // Wait for task to start
        while (!long_task_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // Shutdown should wait for current tasks but not start new ones
        scheduler.shutdown();
        
        // Try to submit a task after shutdown
        bool submitted = scheduler.submit_task([]() {});
        assert(!submitted);  // Should fail
        
        scheduler.join_all();
        assert(!scheduler.is_running());
        
        // The long task should have completed
        assert(long_task_completed.load());
        
        std::cout << "PASSED\n";
    }
    
    static void test_concurrent_task_submission() {
        std::cout << "Test: Concurrent task submission... ";
        
        TaskScheduler scheduler;
        std::atomic<int> total_executed{0};
        
        assert(scheduler.start(4, "ConcurrentTest"));
        
        // Submit tasks from multiple threads
        std::vector<std::thread> submitter_threads;
        const int threads = 3;
        const int tasks_per_thread = 5;
        
        for (int t = 0; t < threads; ++t) {
            submitter_threads.emplace_back([&]() {
                for (int i = 0; i < tasks_per_thread; ++i) {
                    scheduler.submit_task([&total_executed]() {
                        total_executed.fetch_add(1);
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    });
                }
            });
        }
        
        // Wait for all submitter threads
        for (auto& t : submitter_threads) {
            t.join();
        }
        
        // Wait for all tasks to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        assert(total_executed.load() == threads * tasks_per_thread);
        
        scheduler.shutdown();
        scheduler.join_all();
        
        std::cout << "PASSED\n";
    }
};

int main() {
    try {
        TaskSchedulerTest::run_all_tests();
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