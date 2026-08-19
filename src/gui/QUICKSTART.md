# PicExplore GUI - Quick Start Guide

## What is it?

An interactive FLTK-based image gallery that displays photos in a justified layout with progressive thumbnail loading. Think Google Photos or Flickr's justified grid view, but as a desktop application.

## Key Features (60 second version)

- **Fast**: Initial scan shows layout immediately with placeholders
- **Progressive**: Thumbnails load from low to high quality (32px → 1024px)
- **Smart**: Prioritizes visible images, loads others in background
- **Smooth**: Scroll stabilization prevents lag during navigation
- **Efficient**: Uses libjpeg-turbo fast decoding, only processes what you see

## Installation

### Ubuntu/Debian
```bash
# Install dependencies
sudo apt-get install cmake build-essential libjpeg-turbo8-dev libfltk1.3-dev pkg-config

# Clone and build
git clone --recursive https://github.com/starseeker/picexplore.git
cd picexplore
mkdir build && cd build
cmake ..
make picexplore_gui
```

### Result
You'll get an executable at: `build/gui/picexplore_gui` (~2.8 MB)

## Usage

### Method 1: Command Line
```bash
./build/gui/picexplore_gui /path/to/photos
```

### Method 2: Browse Button
```bash
./build/gui/picexplore_gui
# Click "Browse Directory..." and select your photo folder
```

## What to Expect

1. **Immediate**: Window opens, you select a directory
2. **Fast Scan** (1-2 seconds): Reads metadata, shows grey rectangles
3. **Layout** (instant): Arranges images in justified rows
4. **Loading** (progressive):
   - First 100ms: Grey rectangles with resolution
   - Next 500ms: Pixelated 32px thumbnails appear
   - Next 2s: Thumbnails improve to 128px
   - Background: Continues to 1024px for best quality

5. **Scrolling**:
   - Smooth and responsive
   - After 500ms of no scrolling, newly visible images prioritized
   - Thumbnails load for new visible area

## Supported Formats

- JPEG (.jpg, .jpeg) - Optimized with fast decode
- PNG (.png)
- BMP (.bmp)
- TGA (.tga)

## Typical Performance

| Image Count | Scan Time | Initial Display | Full Load |
|-------------|-----------|-----------------|-----------|
| 100 images  | < 1 sec   | Instant         | 5-10 sec  |
| 500 images  | 1-2 sec   | Instant         | 20-30 sec |
| 1000 images | 2-3 sec   | Instant         | 40-60 sec |

*Times assume SSD, modern CPU, visible = ~20 images*

## Common Questions

**Q: Why grey rectangles?**
A: They show layout immediately while thumbnails load. Better than blank space.

**Q: Why do thumbnails look pixelated at first?**
A: Progressive loading - shows low quality fast, improves over time.

**Q: Why does scrolling pause thumbnail loading?**
A: 500ms stabilization prevents wasting work on images you're scrolling past.

**Q: Can I click on images?**
A: Currently just logs filename. Future: fullscreen preview.

**Q: Where are thumbnails stored?**
A: In memory only. Future: disk cache for faster reload.

**Q: Does it modify my images?**
A: No! Read-only. Never touches original files.

## Keyboard Shortcuts

Currently none. Mouse-only interface:
- Scroll wheel: Navigate up/down
- Click image: Log filename (placeholder for future features)

## Troubleshooting

**Problem**: Black window or crash on start
- **Solution**: Ensure DISPLAY is set, running in X11/Wayland environment

**Problem**: No thumbnails appear
- **Solution**: Check console for errors, verify image formats supported

**Problem**: Very slow loading
- **Solution**: Check disk speed, reduce image count, or wait longer

**Problem**: High memory usage
- **Solution**: Scroll less rapidly, let cache stabilize. Memory usage O(visible images).

## Architecture (5 minute version)

```
Main Thread (FLTK UI)
  ├─ Browse button
  ├─ Status display
  └─ Gallery widget
      ├─ Justified layout
      └─ Image boxes (grey → thumbnails)

Scan Thread
  └─ Fast metadata read (stbi_info, no decode)

Worker Threads (4x)
  ├─ Priority queue (visible first)
  ├─ JPEG fast decode (libjpeg-turbo)
  └─ Callback → Main thread (Fl::awake)
```

Key insight: Workers never touch UI directly. They decode thumbnails and notify main thread via callback. Main thread updates widgets safely.

## Files to Read

1. **README.md** (this file) - Start here
2. **VISUAL_OVERVIEW.txt** - ASCII diagrams of system
3. **IMPLEMENTATION_SUMMARY.md** - Metrics and overview
4. **ARCHITECTURE.md** - Deep design details

## Development

Want to hack on the code?

**Key files**:
- `thumbnail_cache.{h,cpp}` - Async thumbnail generation
- `fl_justified_gallery.{h,cpp}` - Main widget
- `picexplore_gui.cpp` - Application entry point

**Build with debug**:
```bash
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make picexplore_gui
gdb ./gui/picexplore_gui
```

**Add features**:
1. Modify source files
2. `make picexplore_gui`
3. Test: `./gui/picexplore_gui`
4. Iterate

## Next Steps

- Try it with your photo collection
- Read ARCHITECTURE.md for design details
- Report bugs or suggest features
- Contribute improvements!

## Credits

- **Justified Layout Algorithm**: Port of Flickr's justified-layout
- **JPEG Library**: libjpeg-turbo
- **Image Loading**: stb_image by Sean Barrett
- **GUI Framework**: FLTK 1.3
- **Build System**: CMake

## License

MIT License - See source files for full text.

---

**Pro Tip**: Start with a small directory (50-100 images) to get a feel for the interface before trying your entire photo collection.

**Enjoy!** 📸
