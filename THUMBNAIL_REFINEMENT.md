# Thumbnail Refinement Pass Implementation

## Overview

The thumbnail refinement pass system improves image quality in the FLTK-based gallery UI by automatically upgrading low-resolution thumbnails to higher-quality versions as they become available.

## Problem Addressed

Previously, the gallery would display low-res thumbnails (e.g., 32px) if higher-res versions weren't yet available. Users would see blurry images even after high-res thumbnails were generated, unless they manually triggered re-requests by scrolling.

## Solution

The refinement system:

1. **Tracks Quality**: For each visible image, tracks the requested display size vs actual thumbnail size used for drawing
2. **Detects Upscaling**: Records when thumbnails are upscaled from fallback (low-res) versions  
3. **Timer-Based Checks**: Uses `Fl::add_timeout` to periodically check visible images for quality improvements
4. **Automatic Upgrades**: When higher-resolution thumbnails become available, triggers redraw to replace upscaled images
5. **Smart Stopping**: Continues refinement until all visible images have optimal quality, then stops the timer

## Implementation Details

### Data Structures

```cpp
struct ThumbnailQualityInfo {
    int requested_width = 0;    // Display width requested
    int requested_height = 0;   // Display height requested
    int actual_width = 0;       // Actual thumbnail width used
    int actual_height = 0;      // Actual thumbnail height used
    bool is_upscaled = false;   // True if upscaled from lower resolution
    bool needs_refinement = false; // True if better quality might be available
};
```

### Key Methods

- `start_refinement_timer()` - Starts FLTK timer for periodic checks
- `stop_refinement_timer()` - Stops timer when refinement complete
- `perform_refinement_pass()` - Checks visible thumbnails for improvements
- `all_visible_thumbnails_optimal()` - Determines if refinement can stop
- `update_thumbnail_quality_info()` - Tracks quality data per image

### Timer Integration

The refinement timer starts automatically after:
- Scroll events (via `update_visibility_and_queue_thumbnails()`)
- Resize events (via `resize()`)
- New thumbnail arrivals (via `process_thread_manager_results()`)

The timer stops when:
- All visible thumbnails have optimal quality
- The widget is destroyed

### Quality Determination

A thumbnail needs refinement if:
- It's upscaled (display size > actual size), OR
- The actual size is significantly smaller than requested (< 80% of requested)

## Usage

The system works automatically without user intervention:

1. User scrolls to new images → timer starts
2. Low-res thumbnails display immediately (no blocking)
3. Timer periodically checks for better quality thumbnails
4. When found, higher-quality versions replace low-res ones
5. Timer stops when all visible images are optimal

## Performance Considerations

- Uses efficient unordered_map for O(1) quality info lookup
- Timer checks every 2 seconds (configurable via `REFINEMENT_CHECK_INTERVAL`)
- Only processes visible images to minimize overhead
- Automatic cleanup prevents memory leaks

## C++17 Compliance

- Uses structured bindings and auto type deduction
- Leverages standard library containers and algorithms
- No external dependencies beyond FLTK's native timer system
- Follows modern C++ best practices with RAII and smart pointers

## Thread Safety

- Uses mutex protection for quality info map
- Integrates with existing thread-safe thumbnail generation system
- FLTK timer callbacks run in main UI thread (thread-safe)

## Testing

The refinement logic is validated by `test_refinement_logic.cpp` which demonstrates:
- Correct identification of upscaled thumbnails
- Proper detection of undersized thumbnails  
- Accurate optimal quality determination
- Refinement improvement tracking