# Thumbnail Request Deduplication

## Overview

The thumbnail worker queue now includes deduplication to prevent multiple identical requests for the same image/hash:size from being processed simultaneously. This eliminates wasted CPU and memory resources by ensuring only one thumbnail generation request per unique hash/size combination is processed at a time.

## Implementation

### Core Components

1. **Tracking Structure**: `std::unordered_set<std::string> in_flight_requests_`
   - Stores cache keys (format: "hash:canonical_size") for requests currently being processed
   - Protected by `std::mutex in_flight_mutex_` for thread safety

2. **Deduplication Logic**: 
   - Before enqueuing: Check if request is already in-flight
   - If duplicate: Skip enqueuing and log debug message
   - If unique: Mark as in-flight and enqueue for processing
   - After completion: Remove from in-flight tracker

### Key Methods

- `is_request_in_flight(cache_key)` - Thread-safe check for existing requests
- `mark_request_in_flight(cache_key)` - Add request to tracking set
- `mark_request_completed(cache_key)` - Remove completed request from tracking set

### Cache Key Format

Cache keys use the format `hash:canonical_size` where:
- `hash` is the unique image content hash
- `canonical_size` is one of the standard thumbnail sizes (32, 64, 128, 256, 512, 1024)

This ensures that:
- Same image + same size = deduplicated (only one request processed)
- Same image + different size = not deduplicated (different cache keys)
- Different image + same size = not deduplicated (different cache keys)

## Benefits

1. **Performance**: Eliminates redundant thumbnail generation work
2. **Memory Efficiency**: Reduces memory usage by avoiding duplicate processing
3. **Responsiveness**: Prevents queue backlog from duplicate requests
4. **Thread Safety**: Concurrent access is properly synchronized

## Thread Safety

The deduplication mechanism is fully thread-safe:
- All access to `in_flight_requests_` is protected by mutex
- Atomic operations ensure consistent state across multiple threads
- Proper cleanup during shutdown prevents memory leaks

## Testing

The implementation includes comprehensive tests:
- `test_thumbnail_deduplication.cpp` - Unit tests for deduplication logic
- `test_deduplication_manual.cpp` - Manual verification with worker threads
- All existing tests continue to pass, ensuring no regressions

## Debug Output

When enabled, the system logs deduplication activity:
```
[DEBUG] ThumbnailWorkers: Marked request in-flight - cache_key: hash123:256 (total in-flight: 1)
[DEBUG] ThumbnailWorkers: Skipping duplicate high priority request - cache_key: hash123:256 already in flight
[DEBUG] ThumbnailWorkers: Marked request completed - cache_key: hash123:256 (total in-flight: 0)
```

This makes it easy to verify the deduplication is working correctly in real scenarios.