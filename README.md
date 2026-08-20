# picexplore

![PicExplore GUI viewing NARA images](doc/nara_example.png)

**PicExplore** is a fast, lightweight image browser, content-based deduplicator, batch scanner, and PDF gallery generator for local photo collections.

It combines an interactive FLTK GUI featuring Flickr-style justified layouts, multi-resolution progressive thumbnail streaming, and deep-zoom tiling with high-throughput headless batch tools backed by an embedded LMDB database.

---

## Quick Start

### 1. Interactive GUI Viewer (Default)
Explore and browse an image directory interactively:
```bash
picexplore /path/to/photos
```

### 2. Headless Batch Scanner
Scan an image collection and build or update the thumbnail database without launching the GUI:
```bash
picexplore --scan /path/to/photos
```

### 3. PDF Gallery Generator
Generate a multi-page justified PDF contact sheet directly from a directory or existing database:
```bash
# Scan and export PDF in one command
picexplore -d /path/to/photos --pdf gallery.pdf

# Export PDF from existing database with custom row height and margins
picexplore --pdf gallery.pdf --row-height 180 --margin 12
```

---

## Features

- **Fluid Justified Layout**: Dynamically arranges photos in clean, aesthetic rows using an optimized justified layout algorithm.
- **Progressive Multi-Resolution Streaming**: Generates and loads thumbnails asynchronously across 6 resolution tiers (32px &rarr; 64px &rarr; 128px &rarr; 256px &rarr; 512px &rarr; 1024px), prioritizing visible viewports.
- **Deep Zoom & Tiling**: Smooth single-image inspection mode with full-resolution rendering, level-of-detail (LOD) tiling cache, and interactive minimap navigator.
- **Content-Based Deduplication**: Employs xxHash (XXH3 128-bit) content hashing to detect identical images across different folders or filenames without duplicate thumbnail generation.
- **Embedded Database Cache**: Fast LMDB storage keeps paths, hashes, and thumbnails indexed for instant subsequent loads.
- **Live Directory Watching**: Automatically detects added, deleted, or modified files in real time (via `inotify` on Linux).
- **Metadata & EXIF Inspector**: Collapsible info panel displaying image dimensions, file size, timestamps, EXIF metadata, duplicate occurrences, and clickable folder breadcrumbs for directory filtering.
- **PDF Contact Sheet Generation**: Vector/raster PDF generator with justified layout, customizable row heights, image spacing, and padding.

---

## GUI Controls & Navigation

| Action | Control | Description |
| :--- | :--- | :--- |
| **Scroll Gallery** | `Mouse Wheel` / `Scrollbar` | Smooth vertical scrolling through image rows |
| **Zoom Gallery** | `Ctrl` + `Mouse Wheel` / `Ctrl` + `+` / `-` | Zoom gallery row heights (`Ctrl` + `0` resets zoom) |
| **Select Image** | `Left Click` | Select an image and display its metadata in Info Panel |
| **Open Full-Res View** | `Double Click` | Enter single-image inspection mode |
| **Zoom in Full View** | `Mouse Wheel` | Zoom in/out at mouse cursor position |
| **Pan in Full View** | `Left Click + Drag` | Pan across the zoomed image (or drag minimap viewport) |
| **Previous / Next Image** | `Left Arrow` / `Right Arrow` | Navigate adjacent images in single-image mode |
| **Exit Full View** | `Escape` | Return to gallery view |
| **Filter by Directory** | Click breadcrumb button | Filters current view to clicked folder (`Ctrl` + `R` resets filter) |
| **Toggle Info Panel** | `View` &rarr; `Information Panel` | Show/hide image details, EXIF, and duplicate list |
| **Toggle Minimap** | `View` &rarr; `Navigator (Minimap)` | Show/hide navigation minimap in full-res view |

---

## CLI Reference

```
Usage:
  picexplore [directory] [OPTIONS]

Positional Arguments:
  directory                    Directory of images to view or scan

Options:
  -h, --help                   Print usage and options
  -d, --directory PATH         Directory of images to view or scan
  -s, --scan                   Run batch scanner headlessly without launching GUI
  --pdf PATH                   Generate PDF gallery from database headlessly
  --db, --database PATH        Path to LMDB database file (default: ~/.cache/picexplore/databases/<hash>.db)
  --row-height N               Target row height in pixels for PDF layout (default: 150)
  --margin N                   Spacing between images in pixels for PDF (default: 10)
  --layout-pad N               Layout padding for all sides in pixels (default: 0)
  --layout-pad-top N           Layout padding top in pixels
  --layout-pad-bottom N        Layout padding bottom in pixels
  --layout-pad-left N          Layout padding left in pixels
  --layout-pad-right N         Layout padding right in pixels
  -v, --verbose                Enable verbose output and performance metrics
```

---

## Supported Image Formats

| Format | Extension | Decoding Pipeline |
| :--- | :--- | :--- |
| **JPEG** | `.jpg`, `.jpeg` | `libjpeg-turbo` with DCT-domain downscaling and TinyEXIF orientation |
| **PNG** | `.png` | Native `libpng` decoder with fast color space conversions |
| **WebP** | `.webp` | Native `libwebp` decoder with hardware-accelerated scaling |
| **TIFF** | `.tif`, `.tiff` | `libtiff` with RGBA converter and floating-point fallback |
| **BMP** | `.bmp` | High-performance `stb_image` decoding |
| **TGA** | `.tga` | High-performance `stb_image` decoding |

---

## Performance & Architecture

- **DCT-Domain JPEG Decoding**: Uses `libjpeg-turbo`'s IDCT scaling factors (1/1, 1/2, 1/4, 1/8) to decode JPEGs directly at requested thumbnail resolutions, saving significant CPU time and memory bandwidth.
- **Priority Queue & Scroll Stabilization**: Prioritizes thumbnail generation for items currently visible in the viewport, waiting 500ms after scrolling stops to prevent pipeline thrashing.
- **Multi-Threaded Worker Pool**: Spawns background worker threads for parallel file inspection, hashing, and thumbnail rendering without blocking the UI.
- **Storage-Optimized Database**: Thumbnails are stored in LMDB as quality-compressed JPEGs (90% quality) with instant memory-mapped lookups.

---

## Building

### Prerequisites

Ensure you have CMake 3.12+, a C++17 compiler, and the required development libraries installed:

**Ubuntu / Debian:**
```bash
sudo apt-get update
sudo apt-get install cmake build-essential pkg-config \
    libfltk1.3-dev libjpeg-turbo8-dev libpng-dev \
    libwebp-dev libtiff-dev zlib1g-dev
```

**Fedora / RHEL:**
```bash
sudo dnf install cmake gcc-c++ pkgconf-pkg-config \
    fltk-devel libjpeg-turbo-devel libpng-devel \
    libwebp-devel libtiff-devel zlib-devel
```

**Arch Linux:**
```bash
sudo pacman -S cmake base-devel pkgconf fltk libjpeg-turbo libpng libwebp libtiff zlib
```

### Build Steps

1. Clone the repository and submodules:
```bash
git clone --recursive https://github.com/starseeker/picexplore.git
cd picexplore
```
*(If already cloned without `--recursive`, run `git submodule update --init --recursive`)*

2. Build with CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

The resulting executable `picexplore` will be in `build/`.

---

## License

PicExplore is open source under the [MIT License](LICENSE).
