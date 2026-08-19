# PicExplore GUI - Interactive Image Gallery

An interactive FLTK-based GUI for browsing image collections with justified layout and progressive thumbnail loading.

## Features

### 1. Incremental Display
- **Fast Pass**: Quickly scans directories and displays image metadata (filename, resolution)
- **Grey Rectangles**: Initially displays placeholder rectangles in justified layout
- **Progressive Loading**: Asynchronously generates and displays thumbnails with increasing quality
- **Quality Levels**: 32px → 64px → 128px → 256px → 512px → 1024px

### 2. Priority Queue System
- **Viewport-Aware**: Prioritizes thumbnails for images visible in the current view
- **Dynamic Reprioritization**: Updates priorities when scrolling
- **Scroll Stabilization**: Waits 500ms after scroll stops before recomputing priorities
- **Anti-Thrashing**: Prevents excessive queue rebuilding during rapid scrolling

### 3. Justified Layout Widget
- **Fl_Justified_Gallery**: Custom FLTK scrollable widget with justified layout
- **Robust Updates**: Properly integrates with FLTK event system for reliable redraw
- **Visibility Detection**: Efficiently determines which images are visible
- **Smooth Scrolling**: Maintains performance even with large image collections

## Architecture

### ThumbnailCache
Manages async thumbnail generation with worker threads:
- Maintains image metadata and thumbnail states
- Implements priority queue for work distribution
- Uses libjpeg-turbo for efficient JPEG partial decoding
- Thread-safe cache with callback system for UI updates

### Fl_Justified_Gallery
FLTK widget for displaying images:
- Extends Fl_Scroll for scrolling support
- Uses justified_layout.hpp for optimal space utilization
- Implements scroll timer for stabilization
- Efficiently redraws only visible portions

### Fl_Image_Box
Individual image display widget:
- Shows grey rectangle placeholder initially
- Progressively updates with better thumbnails
- Displays image resolution in placeholder
- Handles click events

## Building

The GUI is built as part of the main project:

```bash
cd build
cmake ..
make picexplore_gui
```

Requirements:
- FLTK 1.3+
- libjpeg-turbo
- C++17 compiler

## Usage

### Command Line
```bash
# Launch GUI
./picexplore_gui

# Or directly load a directory
./picexplore_gui /path/to/images
```

### Interactive Use
1. Click "Browse Directory..." button
2. Select a folder containing images
3. Wait for fast scan to complete
4. Thumbnails will load progressively, starting with visible images
5. Scroll to browse - thumbnails will load for newly visible images

## Technical Details

### Thumbnail Generation
For JPEG files:
- Uses libjpeg-turbo DCT-domain scaling (1/1, 1/2, 1/4, 1/8)
- Selects optimal scale factor based on target thumbnail size
- Minimizes memory usage and decode time

For other formats (PNG, BMP, TGA):
- Loads full image with stb_image
- Resizes with stb_image_resize2 (high-quality linear interpolation)
- Converts to JPEG for consistent caching

### Performance Optimizations
- Worker thread pool (default: 4 threads)
- Priority queue ensures visible images load first
- Scroll stabilization prevents queue thrashing
- Only visible thumbnails are decoded and rendered
- Efficient FLTK integration minimizes unnecessary redraws

### Memory Management
- Thumbnails stored as compressed JPEG data
- Decoded only when needed for display
- FLTK manages image widget memory automatically
- Progressive loading prevents memory spikes

## Design Rationale

The architecture addresses several key challenges:

1. **Incremental Display**: Users see something immediately (grey rectangles with metadata) rather than waiting for all thumbnails
2. **Progressive Quality**: Thumbnails improve over time, balancing responsiveness with quality
3. **Priority Queue**: Visible content loads first, providing the best user experience
4. **Scroll Stability**: 500ms delay prevents excessive work when scrolling through gallery
5. **Robust Updates**: Proper FLTK integration via Fl::awake ensures thread-safe UI updates

Previous attempts failed due to unreliable update mechanisms. This implementation uses:
- Proper thread-safe callbacks with Fl::awake
- Clear separation between worker threads and UI thread
- Stable widget hierarchy with minimal dynamic creation/destruction
- Event-driven redraw only when thumbnails actually change

## Future Enhancements

Possible improvements:
- Add thumbnail disk cache for faster reloading
- Support for video thumbnail generation
- Image preview/fullscreen view on click
- Sorting and filtering options
- Export selected images
- Integration with existing database.h for persistent storage
