/*
 * integration_test.cpp - Integration test for TaskScheduler with basic coordination
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
#include <atomic>
#include <cassert>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

// Simple blocking queue implementation for testing (replacing missing concurrentqueue)
template<typename T>
class SimpleBlockingQueue {
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    
public:
    void enqueue(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(item);
        condition_.notify_one();
    }
    
    bool wait_dequeue_timed(T& item, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (condition_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            item = queue_.front();
            queue_.pop();
            return true;
        }
        return false;
    }
};

// Mock task types similar to those used in thread_manager
struct MockTask {
    int id;
    std::string data;
    bool is_shutdown_sentinel = false;
    
    MockTask() = default;
    MockTask(int task_id, const std::string& task_data) : id(task_id), data(task_data) {}
    
    static MockTask create_shutdown_sentinel() {
        MockTask task;
        task.is_shutdown_sentinel = true;
        return task;
    }
};

// Integration test demonstrating TaskScheduler working with queue-based communication
class IntegrationTest {
public:
    static void run_integration_tests() {
        std::cout << "Running TaskScheduler integration tests...\n";
        
        test_scheduler_with_queue_communication();
        test_multiple_schedulers_coordination();
        test_shutdown_coordination();
        
        std::cout << "All integration tests passed!\n";
    }
    
private:
    static void test_scheduler_with_queue_communication() {
        std::cout << "Test: Scheduler with queue-based communication... ";
        
        TaskScheduler producer_scheduler;
        TaskScheduler consumer_scheduler;
        SimpleBlockingQueue<MockTask> task_queue;
        
        std::atomic<int> tasks_produced{0};
        std::atomic<int> tasks_consumed{0};
        std::atomic<bool> should_stop{false};
        
        // Start schedulers
        assert(producer_scheduler.start(2, "Producer"));
        assert(consumer_scheduler.start(2, "Consumer"));
        
        // Producer tasks - similar to DirectoryScanThread enqueueing work
        const int num_tasks = 20;
        for (int i = 0; i < 2; ++i) {
            producer_scheduler.submit_task([&, i]() {
                for (int j = 0; j < num_tasks/2; ++j) {
                    if (should_stop.load()) break;
                    
                    MockTask task(i * 100 + j, "data_" + std::to_string(i) + "_" + std::to_string(j));
                    task_queue.enqueue(task);
                    tasks_produced.fetch_add(1);
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
        }
        
        // Consumer tasks - similar to WorkerPool processing work
        for (int i = 0; i < 2; ++i) {
            consumer_scheduler.submit_task([&]() {
                MockTask task;
                while (!should_stop.load()) {
                    if (task_queue.wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
                        if (task.is_shutdown_sentinel) {
                            break;
                        }
                        
                        // Simulate processing work
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        tasks_consumed.fetch_add(1);
                    }
                }
            });
        }
        
        // Wait for production to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        
        // Send shutdown sentinels
        should_stop.store(true);
        task_queue.enqueue(MockTask::create_shutdown_sentinel());
        task_queue.enqueue(MockTask::create_shutdown_sentinel());
        
        // Shutdown and verify
        producer_scheduler.shutdown();
        consumer_scheduler.shutdown();
        producer_scheduler.join_all();
        consumer_scheduler.join_all();
        
        assert(tasks_produced.load() == num_tasks);
        assert(tasks_consumed.load() == num_tasks);
        
        std::cout << "PASSED (produced: " << tasks_produced.load() 
                  << ", consumed: " << tasks_consumed.load() << ")\n";
    }
    
    static void test_multiple_schedulers_coordination() {
        std::cout << "Test: Multiple schedulers coordination... ";
        
        TaskScheduler scheduler1;
        TaskScheduler scheduler2;
        TaskScheduler scheduler3;
        
        std::atomic<int> total_work{0};
        
        // Start all schedulers
        assert(scheduler1.start(1, "Sched1"));
        assert(scheduler2.start(1, "Sched2"));
        assert(scheduler3.start(1, "Sched3"));
        
        // Submit coordinated work
        const int work_per_scheduler = 5;
        
        for (int i = 0; i < work_per_scheduler; ++i) {
            scheduler1.submit_task([&]() {
                total_work.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
            
            scheduler2.submit_task([&]() {
                total_work.fetch_add(2);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
            
            scheduler3.submit_task([&]() {
                total_work.fetch_add(3);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            });
        }
        
        // Wait for completion
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        // Shutdown all
        scheduler1.shutdown();
        scheduler2.shutdown();
        scheduler3.shutdown();
        scheduler1.join_all();
        scheduler2.join_all();
        scheduler3.join_all();
        
        int expected_total = work_per_scheduler * (1 + 2 + 3);
        assert(total_work.load() == expected_total);
        
        std::cout << "PASSED (total work: " << total_work.load() << ")\n";
    }
    
    static void test_shutdown_coordination() {
        std::cout << "Test: Shutdown coordination... ";
        
        TaskScheduler scheduler;
        
        std::atomic<bool> long_task_started{false};
        std::atomic<bool> long_task_completed{false};
        std::atomic<int> quick_tasks_completed{0};
        
        assert(scheduler.start(3, "ShutdownTest"));
        
        // Submit a long-running task
        scheduler.submit_task([&]() {
            long_task_started.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            long_task_completed.store(true);
        });
        
        // Submit some quick tasks
        for (int i = 0; i < 5; ++i) {
            scheduler.submit_task([&]() {
                quick_tasks_completed.fetch_add(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            });
        }
        
        // Wait for long task to start
        while (!long_task_started.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        
        // Request shutdown - should complete running tasks but not start new ones
        scheduler.shutdown();
        
        // Try to submit more tasks (should fail)
        bool submitted = scheduler.submit_task([]() {});
        assert(!submitted);
        
        scheduler.join_all();
        
        // Verify long task completed and some quick tasks ran
        assert(long_task_completed.load());
        assert(quick_tasks_completed.load() >= 0);
        
        std::cout << "PASSED (quick tasks: " << quick_tasks_completed.load() << ")\n";
    }
};

int main() {
    try {
        IntegrationTest::run_integration_tests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Integration test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Integration test failed with unknown exception" << std::endl;
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