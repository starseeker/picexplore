# Logging System Documentation

The picexplore application includes a controllable logging system that allows fine-grained control over debug output through environment variables. This facilitates downstream analysis and debugging by providing organized, categorized logging output.

## Overview

The logging system is implemented in `src/logging.hpp` and `src/logging.cpp` and provides:

- **Category-based logging**: Different categories for different parts of the application
- **Level-based filtering**: Control verbosity with levels 1 (basic) and 2 (verbose)
- **Environment variable control**: Configure logging behavior without recompiling
- **Timestamped output**: All log messages include precise timestamps

## Categories

### BATCH
Controls logging for image batch and layout processing operations.

**Level 1 (Basic)**: 
- Batch processing triggers
- Summary of batch operations
- Basic batch statistics

**Level 2 (Verbose)**:
- Detailed batch queueing information
- Individual image processing steps
- Incremental update details
- Cache operations

### UI
Controls logging for user interface and layout operations.

**Level 1 (Basic)**:
- Layout calculation triggers
- Scroll position changes
- Widget resize events

**Level 2 (Verbose)**:
- Detailed layout calculations
- Thumbnail loading operations
- Redraw operations
- UI event handling

### THREAD
Controls logging for thread management and thumbnail processing.

**Level 1 (Basic)**:
- Thread pool operations
- Thumbnail generation triggers
- Basic worker thread activity

**Level 2 (Verbose)**:
- Detailed thread scheduling
- Individual thumbnail tasks
- Worker thread state changes
- Synchronization operations

### SCAN
Controls logging for directory scanning, file discovery, and database operations.

**Level 1 (Basic)**:
- Scan start/completion events
- Progress summaries
- Error conditions
- Database connection and transaction errors
- Major file processing errors

**Level 2 (Verbose)**:
- Individual file processing
- Database operations
- Metadata extraction details
- Incremental scan updates
- Detailed worker thread operations
- Database write task queueing

### THREAD
Controls logging for thread management, worker pools, and background processing.

**Level 1 (Basic)**:
- Thread pool operations
- Worker thread lifecycle events
- Major thread state changes
- Thumbnail generation failures

**Level 2 (Verbose)**:
- Detailed thread scheduling
- Individual worker tasks
- Task queue operations
- Write thread batching operations
- Synchronization operations

### THUMBNAIL
Controls logging for thumbnail operations (cache, loading, timers, UI updates).

**Level 1 (Basic)**:
- Thumbnail requested from cache/ThreadManager
- Cache miss (requesting from ThreadManager)
- Thumbnail loaded and UI update triggered  
- Timer triggers for refinement and scroll settling
- ThreadManager thumbnail processing completion
- UI placeholder replacement with real thumbnails
- ThumbnailWorker thread lifecycle events
- Thumbnail generation errors

**Level 2 (Verbose)**:
- Cache hit details (rectangle cache, canonical cache)
- Image scaling and downsampling operations
- Thumbnail quality tracking and refinement checks
- Individual thumbnail task queueing and processing
- ThreadWorker task dequeuing and completion
- Detailed timer and generation ID operations
- Database thumbnail lookup operations
- In-flight request tracking

## Environment Variables

### Global Control

- **`PICEXPLORE_LOGGING`**: Sets logging level for all categories (0, 1, or 2)
  - `0`: No logging (default)
  - `1`: Basic logging only
  - `2`: Verbose logging

### Category-Specific Control

Individual categories can be controlled independently:

- **`BATCH_LOGGING`**: Controls batch processing logging (0, 1, or 2)
- **`UI_LOGGING`**: Controls UI and layout logging (0, 1, or 2)
- **`THREAD_LOGGING`**: Controls thread management logging (0, 1, or 2)
- **`SCAN_LOGGING`**: Controls scanning logging (0, 1, or 2)
- **`THUMBNAIL_LOGGING`**: Controls thumbnail operations logging (0, 1, or 2)

**Note**: Category-specific variables override the global setting for that category.

## Usage Examples

### Enable all logging at basic level:
```bash
export PICEXPLORE_LOGGING=1
./picexplore /path/to/images
```

### Enable verbose thumbnail logging only:
```bash
export THUMBNAIL_LOGGING=2
./picexplore /path/to/images
```

### Enable basic thumbnail and UI logging:
```bash
export THUMBNAIL_LOGGING=1
export UI_LOGGING=1
./picexplore /path/to/images
```

### Enable verbose logging for batch and UI, basic for threads:
```bash
export BATCH_LOGGING=2
export UI_LOGGING=2
export THREAD_LOGGING=1
./picexplore /path/to/images
```

### Enable comprehensive thumbnail debugging:
```bash
export THUMBNAIL_LOGGING=2
export THREAD_LOGGING=1
export UI_LOGGING=1
./picexplore /path/to/images
```

### Enable database and scanning debugging:
```bash
export SCAN_LOGGING=2
export THREAD_LOGGING=1
./picexplore /path/to/images
```

### Enable comprehensive worker debugging:
```bash
export SCAN_LOGGING=1
export THREAD_LOGGING=2
export THUMBNAIL_LOGGING=2
./picexplore /path/to/images
```

### Disable all logging (default):
```bash
unset PICEXPLORE_LOGGING BATCH_LOGGING UI_LOGGING THREAD_LOGGING SCAN_LOGGING THUMBNAIL_LOGGING
./picexplore /path/to/images
```

## Output Format

All log messages follow this format:
```
[HH:MM:SS.mmm] [CATEGORY:LEVEL] message
```

**Examples**:
```
[14:32:15.123] [BATCH:1] Processing batch of 25 images
[14:32:15.145] [BATCH:2] Added image to batch, pending count: 1, total added: 26
[14:32:15.156] [UI:1] Recalculating layout for batch of 25 images
[14:32:15.201] [THREAD:1] ThreadManager available, queuing thumbnail requests for 25 new images
[14:32:15.234] [THUMBNAIL:1] Thumbnail cache miss - requesting from ThreadManager, cache_key: abc123:256
[14:32:15.267] [THUMBNAIL:2] Enqueuing high priority thumbnail task - image_index: 5, cache_key: abc123:256x256
[14:32:15.289] [THUMBNAIL:1] Generated real thumbnail successfully, enqueuing to result_queue - image_index: 5
[14:32:15.312] [THUMBNAIL:1] Drawing real thumbnail - path: /path/image.jpg, size: 256x256
[14:32:15.345] [SCAN:1] DatabaseManager: Starting scan of /home/user/photos
[14:32:15.367] [SCAN:2] DatabaseManager: Processing image file: /home/user/photos/IMG_001.jpg
[14:32:15.389] [SCAN:2] DatabaseManager: Extracted metadata - hash: abc123def, aspect_ratio: 1.33
[14:32:15.421] [THREAD:1] WorkerPool: Worker thread started, processing files 0 to 99
[14:32:15.445] [THREAD:2] WorkerPool: Successfully generated thumbnails, enqueuing write task - hash: abc123def
[14:32:15.467] [THUMBNAIL:1] ThumbnailWorkers: Started 4 thumbnail worker threads
[14:32:15.489] [THUMBNAIL:2] ThumbnailWorkers: Selected best thumbnail size 256px for hash: abc123def
```
```

## Programming Interface

### Using the Logger Class

```cpp
#include "logging.hpp"

// Direct logger usage
picexplore::Logger::getInstance().logBatch(picexplore::LogLevel::BASIC, "Batch started");
picexplore::Logger::getInstance().logUI(picexplore::LogLevel::VERBOSE, "Detailed UI info");
```

### Using Convenience Macros

```cpp
#include "logging.hpp"

// Convenience macros (recommended)
LOG_BATCH_BASIC("Processing batch of " + std::to_string(count) + " images");
LOG_BATCH_VERBOSE("Added image to batch, pending count: " + std::to_string(pending));
LOG_UI_BASIC("Recalculating layout");
LOG_UI_VERBOSE("Resizing content widget, new height: " + std::to_string(height));
LOG_THREAD_BASIC("ThreadManager available");
LOG_SCAN_BASIC("Directory scan completed");
LOG_THUMBNAIL_BASIC("Thumbnail loaded");
LOG_THUMBNAIL_VERBOSE("Rectangle cache hit - cache_key: abc123:256x256");
```

## Migration from Legacy Debug Code

The logging system replaces various legacy debug output methods:

### Before:
```cpp
std::cout << "[DEBUG] has_thumbnails set to true" << std::endl;
std::cout << "[DEBUG] drawing placeholder for " << info.path << "\n";

void log_batch_debug(const std::string& message) const {
    // Debug logging disabled in production
}
```

### After:
```cpp
LOG_BATCH_BASIC("has_thumbnails set to true");
LOG_UI_VERBOSE("drawing placeholder for " + info.path);

// No need for conditional compilation - controlled by environment variables
```

## Performance Considerations

- **Zero overhead when disabled**: When logging is disabled (level 0), the overhead is minimal (single boolean check)
- **Lazy initialization**: Environment variables are read only once at first use
- **Thread-safe**: The logger can be safely used from multiple threads
- **Minimal string construction**: Log messages are only formatted when actually output

## Debugging Tips

1. **Start with basic logging**: Use level 1 first to get an overview
2. **Focus on specific categories**: Enable only the categories you're investigating
3. **Use verbose selectively**: Level 2 can produce very detailed output
4. **Redirect output**: Consider redirecting stderr to a file for analysis:
   ```bash
   export BATCH_LOGGING=2
   ./picexplore /path/to/images 2> debug.log
   ```
5. **Filter output**: Use tools like `grep` to filter specific log categories:
   ```bash
   ./picexplore /path/to/images 2>&1 | grep "BATCH:"
   ```

## Thumbnail Debugging Guide

The THUMBNAIL logging category is specifically designed to help diagnose thumbnail loading and display issues. Here are common scenarios and recommended logging levels:

### Debugging Thumbnail Loading Issues

**Problem**: Thumbnails not appearing or taking too long to load
```bash
export THUMBNAIL_LOGGING=1
export THREAD_LOGGING=1
./picexplore /path/to/images
```
Look for:
- `Thumbnail cache miss` messages (indicates ThreadManager requests)
- `Generated real thumbnail successfully` (confirms worker completion)
- `Drawing real thumbnail` vs `Drawing placeholder` (shows UI state)

### Debugging Cache Performance

**Problem**: Suspected cache inefficiency or repeated processing
```bash
export THUMBNAIL_LOGGING=2
./picexplore /path/to/images
```
Look for:
- `Rectangle cache hit/miss` (per-display-size cache)
- `Thumbnail cache hit` (canonical size cache)  
- `Cached in rectangle cache` (new cache entries)

### Debugging Scroll Performance

**Problem**: Thumbnails reloading during scroll or poor scroll performance
```bash
export THUMBNAIL_LOGGING=1
./picexplore /path/to/images
```
Look for:
- `Started scroll settling timer` (scroll detection)
- `Scroll settling timeout - re-evaluating` (post-scroll thumbnail requests)
- `Discarding stale high priority task` (outdated requests being dropped)

### Debugging Thumbnail Quality/Refinement

**Problem**: Blurry thumbnails or refinement not working
```bash
export THUMBNAIL_LOGGING=2
./picexplore /path/to/images
```
Look for:
- `Performing refinement pass` (timer-driven quality checks)
- `Requesting higher-res thumbnail for refinement` (quality improvements)
- `All visible thumbnails optimal` (refinement completion)

### Complete Thumbnail Pipeline Debugging

For comprehensive analysis of the entire thumbnail system:
```bash
export THUMBNAIL_LOGGING=2
export THREAD_LOGGING=1
export UI_LOGGING=1
./picexplore /path/to/images 2> thumbnail_debug.log
```

This captures the complete flow from user interaction through thumbnail generation to UI display.