#include "full_res_loader.h"
#include "stb_image.h"
#include <iostream>

FullResLoader::FullResLoader(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue)
    : update_queue_(update_queue) {
    worker_ = std::thread(&FullResLoader::worker_thread, this);
}

FullResLoader::~FullResLoader() {
    stop_requested_ = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cv_.notify_one();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
}

void FullResLoader::request(size_t image_index, const std::string& filepath) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_index_ = image_index;
    pending_filepath_ = filepath;
    has_request_ = true;
    cv_.notify_one();
}

void FullResLoader::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    has_request_ = false;
}

void FullResLoader::worker_thread() {
    while (!stop_requested_) {
        size_t image_index;
        std::string filepath;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_requested_ || has_request_; });
            
            if (stop_requested_) break;
            
            image_index = pending_index_;
            filepath = pending_filepath_;
            has_request_ = false;
        }

        // Decode
        int width, height, channels;
        unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);
        
        if (data) {
            std::vector<uint8_t> rgb_data(data, data + (width * height * 3));
            stbi_image_free(data);
            
            // Re-check if cancelled during decode
            bool cancelled = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (has_request_) {
                    // A new request came in, so discard this result
                    cancelled = true;
                }
            }
            
            if (!cancelled) {
                update_queue_.enqueue(UpdateEvent::make_full_res_ready(
                    image_index, filepath, rgb_data, width, height));
            }
        } else {
            std::cerr << "FullResLoader failed to decode: " << filepath << std::endl;
        }
    }
}
