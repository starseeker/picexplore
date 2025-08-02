/*
 * task_scheduler.cpp - Centralized task scheduler implementation for picexplore
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
#include "logging.hpp"
#include <algorithm>
#include <chrono>

#ifdef __linux__
#include <pthread.h>
#endif

TaskScheduler::TaskScheduler() 
    : should_stop_(false), running_(false) {
}

TaskScheduler::~TaskScheduler() {
    shutdown();
    join_all();
}

bool TaskScheduler::start(int num_threads, const std::string& thread_name_prefix) {
    if (running_.load()) {
        LOG_THREAD_BASIC("TaskScheduler: Already running");
        return false;
    }
    
    if (num_threads <= 0) {
        num_threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()));
    }
    
    thread_prefix_ = thread_name_prefix;
    should_stop_.store(false);
    worker_threads_.reserve(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        std::string thread_name = thread_prefix_ + "-" + std::to_string(i);
        worker_threads_.emplace_back(&TaskScheduler::worker_thread_main, this, thread_name);
    }
    
    running_.store(true);
    
    LOG_THREAD_BASIC("TaskScheduler: Started " + std::to_string(num_threads) + 
                     " worker threads with prefix '" + thread_prefix_ + "'");
    
    return true;
}

bool TaskScheduler::submit_task(Task task) {
    if (!running_.load() || should_stop_.load()) {
        return false;
    }
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
    }
    
    condition_.notify_one();
    return true;
}

void TaskScheduler::shutdown() {
    if (!running_.load()) {
        return;
    }
    
    LOG_THREAD_BASIC("TaskScheduler: Shutdown requested");
    should_stop_.store(true);
    condition_.notify_all();
}

void TaskScheduler::join_all() {
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
    
    // Clear any remaining tasks
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!task_queue_.empty()) {
            task_queue_.pop();
        }
    }
    
    running_.store(false);
    LOG_THREAD_BASIC("TaskScheduler: All worker threads joined and cleaned up");
}

size_t TaskScheduler::get_thread_count() const {
    return worker_threads_.size();
}

size_t TaskScheduler::get_queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    return task_queue_.size();
}

bool TaskScheduler::is_running() const {
    return running_.load();
}

void TaskScheduler::worker_thread_main(const std::string& thread_name) {
#ifdef __linux__
    // Set thread name for debugging (Linux only)
    pthread_setname_np(pthread_self(), thread_name.c_str());
#endif
    
    LOG_THREAD_BASIC("TaskScheduler: Worker thread '" + thread_name + "' started");
    
    while (!should_stop_.load()) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait for a task or shutdown signal
            condition_.wait(lock, [this] {
                return should_stop_.load() || !task_queue_.empty();
            });
            
            if (should_stop_.load() && task_queue_.empty()) {
                break;
            }
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }
        
        // Execute the task outside the lock
        if (task) {
            try {
                LOG_THREAD_VERBOSE("TaskScheduler: Worker '" + thread_name + "' executing task");
                task();
            } catch (const std::exception& e) {
                LOG_THREAD_BASIC("TaskScheduler: Worker '" + thread_name + "' caught exception: " + std::string(e.what()));
            } catch (...) {
                LOG_THREAD_BASIC("TaskScheduler: Worker '" + thread_name + "' caught unknown exception");
            }
        }
    }
    
    LOG_THREAD_BASIC("TaskScheduler: Worker thread '" + thread_name + "' exiting");
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s