# Thumbnail Request Deduplication

## Overview

The thumbnail worker queue includes priority-aware deduplication to prevent multiple identical requests for the same image/hash:size from being processed simultaneously while maintaining UI responsiveness. This system allows high-priority UI requests to bypass low-priority background requests, ensuring that user interactions remain responsive while still eliminating unnecessary resource usage.

## Implementation

### Core Components

1. **Tracking Structure**: `std::unordered_map<std::string, UIThumbnailTask::Priority> in_flight_requests_`
   - Stores cache keys (format: "hash:canonical_size") with their associated priority for requests currently being processed
   - Protected by `std::mutex in_flight_mutex_` for thread safety

2. **Priority-Aware Deduplication Logic**: 
   - Before enqueuing: Check if request should be allowed based on priority rules
   - **High priority requests**: Can bypass existing low priority requests, but are deduplicated against other high priority requests
   - **Low priority requests**: Are blocked if any request (high or low priority) is already in-flight
   - After completion: Remove from in-flight tracker regardless of priority

### Key Methods

- `should_allow_request(cache_key, priority)` - Priority-aware check for allowing new requests
- `mark_request_in_flight(cache_key, priority)` - Add request to tracking map with priority
- `mark_request_completed(cache_key)` - Remove completed request from tracking map

### Priority Bypass Rules

The system implements the following priority bypass rules to maintain UI responsiveness:

1. **No request in-flight**: Allow any new request (high or low priority)
2. **Low priority request in-flight + new high priority request**: Allow bypass - the high priority request will be processed
3. **High priority request in-flight + new request (any priority)**: Block - deduplicate to prevent resource waste
4. **Low priority request in-flight + new low priority request**: Block - standard deduplication

### Cache Key Format

Cache keys use the format `hash:canonical_size` where:
- `hash` is the unique image content hash
- `canonical_size` is one of the standard thumbnail sizes (32, 64, 128, 256, 512, 1024)

This ensures that:
- Same image + same size = subject to priority-aware deduplication rules
- Same image + different size = not deduplicated (different cache keys)
- Different image + same size = not deduplicated (different cache keys)

## Benefits

1. **UI Responsiveness**: High priority requests (typically from user interactions) can bypass slow background requests
2. **Performance**: Eliminates redundant thumbnail generation work for same-priority requests
3. **Memory Efficiency**: Reduces memory usage by avoiding duplicate processing
4. **Thread Safety**: Concurrent access is properly synchronized

## Priority Bypass Examples

### Example 1: High Priority Bypassing Low Priority
```
1. Low priority request for hash123:256 starts processing
2. User scrolls viewport, triggering high priority request for hash123:256
3. High priority request bypasses the low priority request and gets processed immediately
4. Both requests may complete, but UI remains responsive
```

### Example 2: High Priority Deduplication
```
1. High priority request for hash456:128 starts processing
2. Another high priority request for hash456:128 arrives
3. Second request is deduplicated (blocked) since high priority is already processing
```

## Thread Safety

The priority-aware deduplication mechanism is fully thread-safe:
- All access to `in_flight_requests_` is protected by mutex
- Atomic operations ensure consistent state across multiple threads
- Priority updates are handled atomically during bypass scenarios
- Proper cleanup during shutdown prevents memory leaks

## Testing

The implementation includes comprehensive tests:
- `test_thumbnail_deduplication.cpp` - Unit tests for basic deduplication logic
- `test_priority_bypass.cpp` - Focused tests for priority bypass functionality
- `test_deduplication_manual.cpp` - Manual verification with worker threads
- All existing tests continue to pass, ensuring no regressions

## Debug Output

When enabled, the system logs priority-aware deduplication activity:
```
[DEBUG] ThumbnailWorkers: Marked request in-flight - cache_key: hash123:256 priority: LOW (total in-flight: 1)
[DEBUG] ThumbnailWorkers: Allowing high priority request to bypass low priority - cache_key: hash123:256
[DEBUG] ThumbnailWorkers: Marked request in-flight - cache_key: hash123:256 priority: HIGH (total in-flight: 1)
[DEBUG] ThumbnailWorkers: Skipping duplicate high priority request - cache_key: hash123:256 already in flight at same or higher priority
[DEBUG] ThumbnailWorkers: Marked request completed - cache_key: hash123:256 (total in-flight: 0)
```

This makes it easy to verify that the priority-aware deduplication is working correctly in real scenarios.