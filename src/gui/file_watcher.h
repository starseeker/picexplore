#pragma once

#include <string>
#include "update_events.h"
#include "../third_party/concurrentqueue/concurrentqueue.h"

class FileWatcher {
public:
    virtual ~FileWatcher() = default;

    // Start watching the specified directory recursively
    virtual void start(const std::string& directory, moodycamel::ConcurrentQueue<UpdateEvent>& update_queue, uint64_t session = 0) = 0;

    // Stop watching
    virtual void stop() = 0;
};
