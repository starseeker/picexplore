#include "work_budget.h"
#include "gui/thumbnail_pipeline.h"
#include <cassert>
#include <vector>
int main() {
    std::atomic<unsigned> active{0}, peak{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 16; ++i) workers.emplace_back([&] {
        WorkPermit work;
        assert(work);
        WorkPermit nested;
        assert(nested);
        unsigned current = ++active, previous = peak;
        while (current > previous && !peak.compare_exchange_weak(previous, current)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        --active;
    });
    for (auto& worker : workers) worker.join();
    assert(peak > 0 && peak <= WorkPermit::limit());
    ByteBudget memory(10);
    auto lease = memory.acquire(8);
    assert(lease && !memory.acquire(11));
    std::atomic<bool> stop{false}, finished{false};
    std::thread waiter([&] { assert(!memory.acquire(3, &stop)); finished = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(!finished);
    stop = true; waiter.join();
    lease.reset(); assert(memory.acquire(10));
    moodycamel::ConcurrentQueue<UpdateEvent> queue;
    {
        ThumbnailPipeline pipeline(queue);
        for (size_t i = 0; i < 512; ++i)
            assert(pipeline.request_thumbnail(i, "file", "hash", ThumbQuality::SMALL, true, 1));
        // Coalescing the same image remains possible even at capacity.
        assert(pipeline.request_thumbnail(0, "changed", "hash2", ThumbQuality::LARGE, true, 1, 0, 0, 2));
        assert(!pipeline.request_thumbnail(512, "file", "hash", ThumbQuality::SMALL, true, 1));
        pipeline.set_generation(2);
        assert(pipeline.request_thumbnail(512, "file", "hash", ThumbQuality::SMALL, true, 2));
    }
    // Stalled UI consumption bounds result count and cancellation unblocks shutdown.
    stop = false; finished = false;
    EventPublisher publisher(queue, 1, &stop);
    for (int i = 0; i < 2048; ++i) publisher.enqueue(UpdateEvent::make_scan_progress(i));
    std::thread producer([&] { publisher.enqueue(UpdateEvent::make_scan_complete()); finished = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    assert(!finished);
    stop = true; producer.join();
    UpdateEvent event;
    int received = 0;
    while (queue.try_dequeue(event)) ++received;
    assert(received == 2048);
}
