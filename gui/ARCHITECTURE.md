# PicExplore GUI Architecture

## Overview

This document describes the architecture and design decisions for the interactive FLTK-based image gallery with progressive thumbnail loading.

## Design Goals

1. **Incremental Display**: Show something useful immediately, improve over time
2. **Priority Queue**: Prioritize visible content for best user experience
3. **Scroll Stabilization**: Prevent excessive work during scrolling
4. **Robust Updates**: Reliable UI updates from worker threads
5. **Efficient Memory**: Progressive loading without memory spikes

## Component Architecture

### 1. ThumbnailCache (thumbnail_cache.h/cpp)

**Purpose**: Manages async thumbnail generation with worker thread pool.

**Key Features**:
- Thread-safe cache with mutex protection
- Priority queue for work distribution
- Callback system for UI updates
- Progressive quality improvement

**Data Structures**:
```cpp
ImageMetadata          // Fast scan results (path, dimensions, aspect ratio)
ThumbnailData          // Cached thumbnail (quality level, JPEG data)
ThumbnailWork          // Work item (image index, target quality, priority)
```

**Worker Thread Flow**:
1. Pull work item from priority queue
2. Check if already in progress or better quality exists
3. Generate thumbnail (JPEG fast decode or stb_image)
4. Update cache with result
5. Invoke callback via Fl::awake (thread-safe UI update)

**Priority Queue Logic**:
- Visible images get `is_priority = true`
- Non-visible images get `is_priority = false`
- Priority items sort before non-priority
- Within same priority, earlier indices first

### 2. Fl_Justified_Gallery (fl_justified_gallery.h/cpp)

**Purpose**: Main scrollable widget displaying justified layout of images.

**Key Features**:
- Custom FLTK widget extending Fl_Scroll
- Scroll stabilization timer (500ms)
- Viewport visibility calculation
- Dynamic thumbnail updates

**Layout Process**:
1. Receive thumbnail cache reference
2. Build Item list from image metadata
3. Run justified_layout algorithm
4. Create Fl_Image_Box widgets for each box
5. Register thumbnail update callback

**Scroll Handling**:
```cpp
handle(FL_MOUSEWHEEL):
  Cancel existing timer
  Start new 500ms timer
  
scroll_timer_callback:
  Calculate visible indices
  Call cache->prioritize_visible_images()
```

**Update Flow**:
```cpp
Worker Thread:
  Generate thumbnail
  Invoke callback with (index, quality)
  
Main Thread (via Fl::awake):
  Get ThumbnailData from cache
  Update corresponding Fl_Image_Box
  Call box->redraw()
```

### 3. Fl_Image_Box (fl_justified_gallery.h/cpp)

**Purpose**: Widget for displaying individual image thumbnails.

**States**:
1. **Grey Rectangle**: Initial state, shows dimensions
2. **Low Quality**: Shows 32px or 64px thumbnail
3. **Medium Quality**: Shows 128px or 256px thumbnail
4. **High Quality**: Shows 512px or 1024px thumbnail

**Drawing Logic**:
- Draw border
- If thumbnail available: decode JPEG, scale to fit, draw centered
- If no thumbnail: draw grey rectangle with resolution text
- Only redraw when new thumbnail arrives

### 4. PicExploreWindow (picexplore_gui.cpp)

**Purpose**: Main application window with UI controls.

**Components**:
- Browse button for directory selection
- Status box for progress messages
- Gallery widget for image display

**Directory Scanning**:
1. User selects directory
2. Launch background thread
3. Fast scan: read dimensions with stb_info (no full decode)
4. Build ImageMetadata list
5. Initialize cache on main thread via Fl::awake
6. Set cache in gallery widget
7. Gallery triggers layout and initial work queue

## Threading Model

### Thread Responsibilities

**Main Thread (FLTK Event Loop)**:
- Handle UI events (scroll, click, resize)
- Update widgets (redraw)
- Receive callbacks from workers via Fl::awake

**Scan Thread**:
- Fast directory scan
- Read image dimensions without full decode
- Pass results to main thread

**Worker Threads (4 default)**:
- Pull work from priority queue
- Generate thumbnails
- Update cache
- Notify main thread via callback

### Thread Safety

**Cache Access**:
- `cache_mutex_` protects thumbnails map
- Read/write operations properly locked
- No holding locks during long operations

**Work Queue**:
- `queue_mutex_` protects priority queue
- `queue_cv_` for worker notification
- Properly cleared and rebuilt during reprioritization

**Callbacks**:
- Workers invoke callback with `(index, quality)`
- Callback uses `Fl::awake` to safely update UI
- Avoids direct FLTK calls from worker threads

## Progressive Loading Strategy

### Quality Levels
```
NONE (0)   → Grey rectangle with dimensions
TINY (32)  → First visible thumbnail
SMALL (64) → Improved quality
MEDIUM (128) → Good quality for most displays
LARGE (256) → High quality
XLARGE (512) → Very high quality
FULL (1024) → Maximum quality
```

### Progression Logic

**Initial Queue Population**:
- All images queued at TINY level
- Non-priority (not visible)

**Visibility Change**:
- Visible images promoted to priority
- Request next quality level
- Non-visible demoted to non-priority

**Quality Advancement**:
- When thumbnail completes, if still visible, queue next level
- Progression: NONE → 32 → 64 → 128 → 256 → 512 → 1024
- Stop at FULL or when image no longer visible

### JPEG Fast Decode

For JPEG files, libjpeg-turbo DCT-domain scaling:
```cpp
Target 32-64px:   scale_denom = 8  (1/8 size)
Target 65-128px:  scale_denom = 4  (1/4 size)
Target 129-256px: scale_denom = 2  (1/2 size)
Target >256px:    scale_denom = 1  (full size)
```

Benefits:
- Faster decode (skip DCT blocks)
- Lower memory usage
- Better cache utilization

## Scroll Stabilization

### Problem
Without stabilization:
- Rapid scrolling triggers continuous reprioritization
- Queue thrashes as priorities change
- Workers waste time on images that scroll out of view
- Poor responsiveness

### Solution
500ms timer-based stabilization:

```cpp
On scroll event:
  1. Cancel existing timer
  2. Start new 500ms timer
  3. Don't reprioritize yet

After 500ms of no scrolling:
  1. Timer fires
  2. Calculate visible indices
  3. Clear and rebuild priority queue
  4. Workers process stable view
```

Benefits:
- Smooth scrolling without lag
- Workers focus on stable content
- Reduced queue churn
- Better overall responsiveness

## Visibility Detection

Algorithm to find visible image boxes:

```cpp
int scroll_y = gallery->yposition();
int view_h = gallery->h();

for each image_box:
  int box_y = box->y() - gallery->y();
  int box_h = box->h();
  
  if (box_y + box_h >= scroll_y && 
      box_y <= scroll_y + view_h):
    // Box intersects viewport
    visible.push_back(index)
```

Efficient:
- O(n) scan of all boxes
- Only called after scroll stabilization
- Clear geometric intersection test

## Justified Layout Integration

Uses existing `justified_layout.hpp`:

```cpp
// Build item list
for each image:
  Item item;
  item.ar = aspect_ratio;
  items.push_back(item);

// Calculate layout
LayoutCfg cfg;
cfg.w = gallery_width;
cfg.rh = 150;  // target row height
JustifiedLayout layout(items, cfg);

// Get positioned boxes
boxes = layout.boxes();

// Create widgets
for each box:
  Fl_Image_Box* widget = new Fl_Image_Box(
    box.l, box.t, box.w, box.h, index);
```

Benefits:
- Consistent with PDF generation
- Optimal space utilization
- Maintains aspect ratios
- Professional appearance

## Error Handling

### Thumbnail Generation Failures
- Catch JPEG decode errors with setjmp/longjmp
- Fall back to stb_image for non-JPEG or failed JPEG
- Mark thumbnail as failed, don't retry
- Show grey rectangle permanently for failed images

### File Access Errors
- Handle missing files gracefully
- Skip unreadable files during scan
- Log errors to stderr
- Continue processing other images

### Memory Management
- Use RAII with unique_ptr for FLTK widgets
- Automatic cleanup on destruction
- No manual memory management
- Exception-safe design

## Performance Characteristics

### Memory Usage
- **Metadata**: ~100 bytes per image
- **Cached Thumbnail**: Variable, typically 5-50KB JPEG
- **Decoded Display**: Only visible thumbnails decoded
- **Total**: Scales with image count and viewport size

### CPU Usage
- **Scan**: Fast, O(n) with n = file count
- **Thumbnail Gen**: Parallel, 4 workers
- **JPEG Decode**: Efficient with turbo scaling
- **Layout**: O(n) with n = image count
- **Redraw**: Only visible region

### I/O Patterns
- **Sequential Scan**: Directory traversal
- **Random Read**: Thumbnail generation (priority order)
- **No Write**: Read-only gallery (no database updates)

## Future Enhancements

### Persistence
- Save thumbnails to database
- Faster reload on directory revisit
- Hash-based deduplication

### Advanced Features
- Image selection/multi-select
- Metadata display panel
- Sort/filter options
- Export selected images
- Fullscreen preview

### Performance
- Disk cache for thumbnails
- Lazy metadata loading
- Virtual scrolling for huge collections
- GPU-accelerated rendering

### User Experience
- Zoom controls
- Grid/list view toggle
- Customizable row height
- Drag-and-drop support
- Keyboard navigation

## Lessons Learned

### What Works
✓ Progressive loading gives immediate feedback
✓ Priority queue focuses work on visible content
✓ Scroll stabilization prevents thrashing
✓ Fl::awake provides reliable thread-safe updates
✓ Justified layout looks professional

### What Didn't Work Previously
✗ Directly updating FLTK widgets from worker threads
✗ Creating/destroying widgets dynamically during updates
✗ Reprioritizing on every scroll event
✗ Loading all thumbnails before display
✗ Synchronous thumbnail generation

### Key Insights
- FLTK requires all widget operations on main thread
- Use Fl::awake for thread-safe cross-thread communication
- Stability (timer) more important than instant response
- Progressive loading better than blocking loads
- Grey rectangles provide useful placeholder content

## Conclusion

The architecture successfully addresses all design goals:

1. **Incremental Display**: Grey rectangles → progressive thumbnails
2. **Priority Queue**: Visible images load first
3. **Scroll Stabilization**: 500ms timer prevents thrashing
4. **Robust Updates**: Fl::awake ensures reliable thread-safe updates
5. **Efficient Memory**: Progressive loading, only decode visible

The implementation provides a responsive, professional image gallery that scales well to large collections while maintaining smooth scrolling and clear visual feedback.
