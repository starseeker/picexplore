/*
 * Queue Usage Patterns and Shutdown Coordination
 * ============================================
 * 
 * This document describes the blocking queue patterns used in picexplore
 * to eliminate CPU-wasting polling loops and ensure clean shutdown.
 * 
 * 
 * BLOCKING QUEUE PATTERN
 * =====================
 * 
 * All worker threads now use moodycamel::BlockingConcurrentQueue instead of
 * the polling-based moodycamel::ConcurrentQueue pattern.
 * 
 * OLD PATTERN (CPU-wasting):
 * --------------------------
 * while (!should_stop) {
 *     Task task;
 *     if (queue.try_dequeue(task)) {
 *         // process task
 *     } else {
 *         std::this_thread::sleep_for(std::chrono::milliseconds(10));  // WASTE!
 *     }
 * }
 * 
 * NEW PATTERN (Efficient blocking):
 * ---------------------------------
 * while (!should_stop) {
 *     Task task;
 *     if (queue.wait_dequeue_timed(task, std::chrono::milliseconds(100))) {
 *         if (task.is_shutdown_sentinel) {
 *             break;  // Clean exit
 *         }
 *         // process task
 *     }
 *     // No explicit sleep - wait_dequeue_timed handles blocking efficiently
 * }
 * 
 * 
 * SHUTDOWN SENTINEL SYSTEM
 * =======================
 * 
 * Each task type supports shutdown sentinels for coordinated thread termination:
 * 
 * Task Structures:
 * - ThumbnailGenerationTask::is_shutdown_sentinel
 * - WriteTask::SHUTDOWN enum value
 * - UIThumbnailTask::is_shutdown_sentinel  
 * - UIDrawTask::is_shutdown_sentinel
 * - ThumbnailNotification::is_shutdown_sentinel
 * 
 * Static Factory Methods:
 * - Task::create_shutdown_sentinel() creates sentinel instances
 * 
 * 
 * SHUTDOWN COORDINATION SEQUENCE
 * =============================
 * 
 * ThreadManager::shutdown_all() coordinates the complete shutdown:
 * 
 * 1. Stop new work generation:
 *    - scan_thread_->stop_scan()
 * 
 * 2. Enqueue shutdown sentinels to all blocking queues:
 *    - thumbnail_gen_queue_ (one per worker thread)
 *    - write_queue_ (one sentinel)
 *    - high_priority_queue_ & low_priority_queue_ (one per worker thread each)
 *    - result_queue_ (one sentinel)
 * 
 * 3. Set stop flags:
 *    - worker_pool_->stop_workers()
 *    - writer_thread_->stop_writing()
 *    - thumbnail_workers_->stop_workers()
 * 
 * 4. Join all threads:
 *    - Threads should exit cleanly after processing shutdown sentinels
 *    - No indefinite blocking since sentinels wake all waiting threads
 * 
 * 
 * QUEUE TYPES AND USAGE
 * ====================
 * 
 * 1. thumbnail_gen_queue_ (BlockingConcurrentQueue<ThumbnailGenerationTask>)
 *    - Coordinates thumbnail generation between DirectoryScanThread and WorkerPool
 *    - Workers use wait_dequeue_timed(100ms) for responsiveness
 * 
 * 2. write_queue_ (BlockingConcurrentQueue<WriteTask>)
 *    - Batched database writes from WorkerPool to WriterThread
 *    - Writer uses wait_dequeue_timed(100ms) + try_dequeue for batching
 * 
 * 3. high_priority_queue_, low_priority_queue_ (BlockingConcurrentQueue<UIThumbnailTask>)
 *    - UI thumbnail requests with priority handling
 *    - Workers check high priority first (50ms timeout), then low priority (50ms timeout)
 * 
 * 4. result_queue_ (BlockingConcurrentQueue<UIDrawTask>)
 *    - Completed thumbnails from ThumbnailWorkers to UI
 *    - UI uses try_dequeue (non-blocking) for results
 * 
 * 5. thumbnail_notifications_ (BlockingConcurrentQueue<ThumbnailNotification>)
 *    - Thumbnail readiness notifications in Fl_JustifiedLayout
 *    - May use blocking patterns if needed for UI updates
 * 
 * 
 * BENEFITS
 * =======
 * 
 * 1. CPU Efficiency:
 *    - Eliminates sleep-based polling loops
 *    - Threads block efficiently until work is available
 *    - Reduces unnecessary CPU cycles
 * 
 * 2. Responsive Shutdown:
 *    - Shutdown sentinels wake blocked threads immediately
 *    - No threads left waiting indefinitely
 *    - Clean termination without force-killing threads
 * 
 * 3. Improved Latency:
 *    - Work items processed immediately when available
 *    - No polling delay (10-100ms) before processing
 * 
 * 4. Robust Threading:
 *    - Explicit coordination mechanisms
 *    - Timeout-based responsiveness to shutdown signals
 *    - Maintainable thread lifecycle management
 */