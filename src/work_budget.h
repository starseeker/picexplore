#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

// Shared across subsystems, so exporting, scanning and viewing do not each
// consume a full hardware_concurrency() pool. Nested helpers reuse a permit.
class WorkPermit {
    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        unsigned active = 0, waiting_interactive = 0;
    };
    static State& state() { static State value; return value; }
    static unsigned& depth() { static thread_local unsigned value = 0; return value; }
    bool acquired_ = false, outer_ = false;
public:
    static unsigned limit() { return std::max(1u, std::min(4u, std::thread::hardware_concurrency())); }
    explicit WorkPermit(const std::atomic<bool>* stop = nullptr, bool interactive = false) {
        if (depth()) { ++depth(); acquired_ = true; return; }
        auto& s = state();
        std::unique_lock<std::mutex> lock(s.mutex);
        if (interactive) ++s.waiting_interactive;
        while (!(stop && stop->load()) && (s.active >= limit() || (!interactive && s.waiting_interactive)))
            s.wake.wait_for(lock, std::chrono::milliseconds(20));
        if (interactive) --s.waiting_interactive;
        if (stop && stop->load()) { s.wake.notify_all(); return; }
        ++s.active; ++depth(); acquired_ = outer_ = true;
    }
    ~WorkPermit() {
        if (!acquired_) return;
        --depth();
        if (outer_) { auto& s = state(); std::lock_guard<std::mutex> lock(s.mutex); --s.active; s.wake.notify_all(); }
    }
    explicit operator bool() const { return acquired_; }
    WorkPermit(const WorkPermit&) = delete;
    WorkPermit& operator=(const WorkPermit&) = delete;
};

class ByteBudget {
    std::mutex mutex_;
    std::condition_variable wake_;
    size_t used_ = 0;
    const size_t limit_;
public:
    explicit ByteBudget(size_t limit) : limit_(limit) {}
    struct Lease {
        ByteBudget& owner; size_t bytes;
        Lease(ByteBudget& owner, size_t bytes) : owner(owner), bytes(bytes) {}
        ~Lease() { std::lock_guard<std::mutex> lock(owner.mutex_); owner.used_ -= bytes; owner.wake_.notify_all(); }
    };
    std::shared_ptr<Lease> acquire(size_t bytes, const std::atomic<bool>* stop = nullptr) {
        if (bytes > limit_) return {};
        std::unique_lock<std::mutex> lock(mutex_);
        while (!(stop && stop->load()) && used_ > limit_ - bytes) wake_.wait_for(lock, std::chrono::milliseconds(20));
        if (stop && stop->load()) return {};
        used_ += bytes;
        return std::make_shared<Lease>(*this, bytes);
    }
    static ByteBudget& decode() { static ByteBudget budget(512ULL * 1024 * 1024); return budget; }
    // Each event pays a fixed overhead as well as its payload, bounding both
    // event count and queued RGB bytes. Reservations follow copied events.
    static ByteBudget& events() { static ByteBudget budget(256ULL * 1024 * 1024); return budget; }
};
