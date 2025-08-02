# Task Scheduler Architecture

This document describes the centralized task scheduler implementation in picexplore.

## Overview

The `TaskScheduler` class provides a centralized thread pool-based task execution system that replaces the previous ad-hoc thread management across various worker components.

## Design Goals

- **Centralized Management**: All thread creation and lifecycle management happens in one place
- **Generic Task Interface**: Tasks are submitted as `std::function<void()>` lambdas
- **Clean Shutdown**: Proper shutdown coordination with all worker threads
- **Thread Safety**: Safe task submission and execution from multiple threads
- **Resource Control**: Configurable thread pool sizes per component

## Components Refactored

### TaskScheduler

The core scheduler class provides:

- `start(num_threads, thread_name_prefix)`: Start the thread pool
- `submit_task(std::function<void()>)`: Submit a task for execution  
- `shutdown()`: Signal all threads to stop
- `join_all()`: Wait for all threads to complete

### Refactored Classes

All worker classes have been migrated from managing their own `std::thread` instances to using the centralized scheduler:

1. **WorkerPool**: Thumbnail generation workers
2. **ThumbnailWorkers**: UI thumbnail generation with priority queues
3. **WriterThread**: Database write operations
4. **DirectoryScanThread**: Directory scanning coordination
5. **UpdateMonitorThread**: UI progress updates

## Benefits

- **Reduced Code Duplication**: Thread lifecycle logic is centralized
- **Better Resource Management**: Controlled thread creation per component
- **Improved Debugging**: Named threads for easier debugging
- **Consistent Shutdown**: Unified shutdown coordination across all components
- **Future Extensibility**: Easy to add new scheduled components

## Migration Strategy

The refactoring maintains all existing functionality:

- Queue-based communication between components is preserved
- Shutdown sentinel mechanisms continue to work
- All existing APIs remain unchanged
- Performance characteristics are maintained

## Testing

The `TaskScheduler` includes comprehensive unit tests covering:

- Basic task execution
- Multiple concurrent tasks
- Shutdown and cleanup procedures
- Concurrent task submission from multiple threads

## Future Enhancements

The centralized scheduler provides a foundation for:

- Task prioritization across components
- Load balancing and work stealing
- Performance monitoring and metrics
- Dynamic thread pool sizing
- Integration with external schedulers (TBB, etc.)