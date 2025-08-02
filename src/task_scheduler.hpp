/*
 * task_scheduler.hpp - Centralized task scheduler for picexplore
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

#pragma once

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <vector>
#include <string>

/**
 * Centralized Task Scheduler for picexplore
 * 
 * This class provides a thread pool-based task scheduler that replaces 
 * the ad-hoc thread management in the existing worker classes.
 * 
 * Features:
 * - Generic task submission using std::function
 * - Configurable thread pool size
 * - Clean shutdown coordination
 * - Thread naming for debugging
 * - Task queue management
 */
class TaskScheduler {
public:
    using Task = std::function<void()>;
    
    TaskScheduler();
    ~TaskScheduler();
    
    // Non-copyable, non-movable
    TaskScheduler(const TaskScheduler&) = delete;
    TaskScheduler& operator=(const TaskScheduler&) = delete;
    TaskScheduler(TaskScheduler&&) = delete;
    TaskScheduler& operator=(TaskScheduler&&) = delete;
    
    /**
     * Start the task scheduler with specified number of worker threads
     * @param num_threads Number of worker threads (0 = auto-detect)
     * @param thread_name_prefix Prefix for thread names (for debugging)
     * @return true if started successfully
     */
    bool start(int num_threads = 0, const std::string& thread_name_prefix = "TaskWorker");
    
    /**
     * Submit a task for execution
     * @param task The task to execute (std::function<void()>)
     * @return true if task was queued successfully
     */
    bool submit_task(Task task);
    
    /**
     * Request shutdown of the scheduler
     * This signals all worker threads to stop after completing current tasks
     */
    void shutdown();
    
    /**
     * Wait for all worker threads to complete
     */
    void join_all();
    
    /**
     * Get the number of worker threads
     */
    size_t get_thread_count() const;
    
    /**
     * Get the number of queued tasks
     */
    size_t get_queue_size() const;
    
    /**
     * Check if the scheduler is running
     */
    bool is_running() const;
    
private:
    void worker_thread_main(const std::string& thread_name);
    
    std::vector<std::thread> worker_threads_;
    std::queue<Task> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> should_stop_;
    std::atomic<bool> running_;
    std::string thread_prefix_;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s