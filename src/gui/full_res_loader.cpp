#include "full_res_loader.h"
#include "utils.h"
#include "stb_image.h"
#include <iostream>
#include <filesystem>
#include <algorithm>

FullResLoader::FullResLoader(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue, uint64_t session)
    : update_queue_(update_queue, session) {
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

void FullResLoader::request(size_t image_index, const std::string& filepath, uint64_t revision, uint64_t request) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cancellation_;
    pending_token_ = {update_queue_.session(), image_index, revision, request};
    pending_index_ = image_index;
    pending_filepath_ = filepath;
    has_request_ = true;
    cv_.notify_one();
}

void FullResLoader::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cancellation_;
    has_request_ = false;
}

void FullResLoader::worker_thread() {
    while (!stop_requested_) {
        size_t image_index;
        std::string filepath;
        JobToken token;
        uint64_t cancellation;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_requested_ || has_request_; });
            
            if (stop_requested_) break;
            
            token = pending_token_;
            cancellation = cancellation_;
            image_index = pending_index_;
            filepath = pending_filepath_;
            has_request_ = false;
        }

        auto decoded = decode_image(filepath, 0, 0, &stop_requested_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_requested_ || cancellation != cancellation_) continue;
        }
        if (decoded) {
            update_queue_.enqueue(UpdateEvent::make_full_res_ready(image_index, filepath,
                std::move(decoded.rgb), decoded.width, decoded.height), token);
        } else {
            auto event = UpdateEvent::make_message(decoded.status == DecodeStatus::CANCELLED ?
                UpdateEvent::Type::JOB_CANCELLED : UpdateEvent::Type::FULL_RES_FAILED, decoded.error);
            event.full_res.image_index = image_index;
            event.full_res.filepath = filepath;
            update_queue_.enqueue(std::move(event), token);
        }
    }
}
