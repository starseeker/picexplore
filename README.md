# picexplore

![PicExplore GUI viewing NARA images with hierarchical treemap](doc/nara_hierarchy_treemap.png)
*Hierarchical (Nested) Treemap: multi-level directory clustering with depth-themed container frames, folder headers, and square thumbnail mosaics.*

![PicExplore GUI viewing NARA images with flat treemap](doc/nara_treemap.png)
*Flat Squarified Treemap: full-collection thumbnail mosaic proportionally weighted by file size or pixel dimensions.*

![PicExplore GUI viewing NARA images with default justified layout](doc/nara_example.png)
*Justified Grid Layout: fluid, multi-resolution row-based photo gallery.*

![PicExplore GUI viewing NARA images with SIFT sorting order](doc/nara_sift.png)
*Justified Grid Layout: SIFT image similarity sorting.*

![PicExplore GUI viewing large NARA image](doc/nara_large_image_viewing.png)
*Single-Image Deep Zoom: full-resolution inspection with level-of-detail (LOD) tiling and interactive minimap navigator.*

---

**PicExplore** is a high-performance, lightweight image browser and PDF gallery generator for local photo collections.

It combines an interactive FLTK GUI featuring Flickr-style justified layouts, flat and hierarchical squarified treemaps with high-throughput headless batch tools backed by an embedded LMDB database.

---

## Quick Start

### 1. Interactive GUI Viewer (Default)
Explore and browse an image directory or specific files interactively:
```bash
# Browse directory recursively (default)
picexplore /path/to/photos

# Browse current directory non-recursively
picexplore -n /path/to/photos

# Browse specific image files or glob patterns
picexplore *.jpg "vacation/**/*.png"
```

### 2. Headless Batch Scanner
Scan an image collection and build or update the thumbnail database without launching the GUI:
```bash
picexplore --scan /path/to/photos
```

### 3. PDF Gallery Generator
Generate multi-page contact sheets directly from a directory or existing database:
```bash
# Export standard justified grid PDF
picexplore -d /path/to/photos --pdf gallery.pdf

# Export Flat Treemap PDF with 3D cushion shading
picexplore -d /path/to/photos --pdf treemap.pdf --layout treemap --style cushion

# Export Hierarchical Treemap PDF (Letter, Landscape, 300 DPI)
picexplore -d /path/to/photos --pdf hier.pdf --layout hierarchical-treemap --orientation landscape --paper-size letter
```

---

## Key Features

- **Triple Interactive Layouts**:
  - **Fluid Justified Grid** (<kbd>Ctrl+1</kbd>): Dynamically arranges photos in clean, aesthetic rows using an optimized justified layout algorithm. Includes scroll-anchored zooming to prevent visual drift during row resizing.
  - **Flat Squarified Treemap** (<kbd>Ctrl+2</kbd>): Visualizes all photos in a flat 2D squarified treemap with zero-allocation $O(1)$ ratio evaluations (<1 ms for 10,000+ items). Proportionally scales rectangles by **File Size** (bytes), **Pixel Area** (megapixels), **Duplicate Count**, or **Equal Size**.
  - **Hierarchical (Nested) Treemap** (<kbd>Ctrl+3</kbd>): Recursively clusters images by directory hierarchy into nested subfolder containers with depth-themed border frames and directory labels. Supports **interactive directory drill-down** (click any folder header to zoom in) and **parent directory navigation** (click the top parent bar to navigate back up).
- **SIFT Visual Similarity Search**:
  - Embedded, zero-dependency C++ SIFT feature engine (derived from VLFeat) compiled directly into the executable.
  - **Fast Mode**: Instant whole-collection similarity ranking using 128-dimensional global Root-SIFT vectors and cosine distance (<5 ms for thousands of images).
  - **High Accuracy Mode**: Two-stage search combining global vector pre-filtering with fine-grained Lowe's ratio test keypoint matching.
  - **Configurable Extraction Resolution**: Standard (256px) for fast general indexing or Fine Detail (512px) for intricate blueprints, drawings, and textures.
  - **Right-Click Instant Search**: Right-click any image in the collection to immediately sort the entire gallery by visual similarity.
- **Triple Treemap Visual Styles**:
  - **All Thumbnails** *(Default)*: Center-cropped square thumbnail mosaic rendered across all tiles down to 1px with customizable hierarchy density thresholds (1px to 32px).
  - **Cushion Treemap**: 3D shaded cushion treemap based on the classic van Wijk & van de Wetering algorithm, accumulating multi-level parabolic height profiles down the directory tree with real-time Lambertian diffuse lighting.
  - **File Type Colors**: Categorical color-coded cards with file extension badges (JPG, PNG, WebP, GIF, TIFF, BMP, TGA, PDF, SVG) and a live status-bar color key.
- **Deep Zoom & LOD Tiling for Gigapixel Images**:
  - Smooth single-image inspection mode with level-of-detail (LOD) JPEG tile streaming for extreme resolutions (tested on 60,000+ pixel blueprints, panoramas, and aerial scans).
  - Smooth fixed-point bilinear interpolation for base overviews and anti-aliased $2\times 2$ box downsampling for tiles to keep fine lines, text, and hatches razor sharp.
  - Hardware-adaptive tile cache dynamically scaling up to 1.25 GB / $\sim$1,700 tiles in RAM.
  - Interactive minimap navigator with draggable viewport indicator box.
- **Progressive Multi-Resolution Streaming**: Generates and loads thumbnails asynchronously across 6 resolution tiers (32px &rarr; 64px &rarr; 128px &rarr; 256px &rarr; 512px &rarr; 1024px), prioritizing visible viewport regions.
- **Content-Based Deduplication**: Employs xxHash (XXH3 128-bit) content hashing to detect identical images across different folders or filenames without duplicate thumbnail generation. Includes a toggleable filter to hide redundant copies in flat views while keeping full duplicate paths accessible in the Info Panel.
- **High-Performance LMDB Database**: Fast embedded LMDB storage caches paths, hashes, EXIF metadata, aspect-ratio thumbnails, pre-cropped square thumbnails (`hash:sq128`, `hash:sq64`), and SIFT descriptor vectors with lock-free parallel read concurrency across all CPU cores.
- **Live Directory Watching**: Automatically detects added, deleted, or modified files in real time (via `inotify` on Linux).
- **Metadata & EXIF Inspector**: Collapsible info panel displaying image dimensions, file size, timestamps, EXIF metadata, duplicate occurrences, customizable font sizes (11pt to 24pt, or custom), and clickable folder breadcrumbs for directory filtering.
- **Comprehensive PDF Export**: Interactive dialog (<kbd>Ctrl+E</kbd>) and CLI exporter supporting Justified Grid, Flat Treemap, and Hierarchical Treemap layouts across diverse paper sizes (Letter, Legal, Tabloid, A4–A1), orientations, margins, and print DPIs.

---

## GUI Controls & Navigation

| Action | Control | Description |
| :--- | :--- | :--- |
| **Switch to Justified Grid** | `Ctrl` + `1` | Switch to row-based justified photo gallery |
| **Switch to Flat Treemap** | `Ctrl` + `2` | Switch to flat squarified treemap collection view |
| **Switch to Hierarchical Treemap** | `Ctrl` + `3` | Switch to nested directory hierarchy treemap view |
| **Open Directory** | `Ctrl` + `O` | Open file chooser dialog to browse a new directory |
| **Export as PDF** | `Ctrl` + `E` | Open interactive PDF export dialog |
| **Rescan & Retry** | `F5` | Rescan directory, check timestamps, and reload missing tiles |
| **Exit Application** | `Ctrl` + `Q` | Quit PicExplore |
| **Drill Down into Directory** | `Left Click` on folder header | Zoom into specific subdirectory in Hierarchical Treemap |
| **Navigate to Parent Directory** | `Left Click` on top parent bar | Zoom out to parent directory (`Ctrl` + `R` resets to root) |
| **Scroll Gallery** | `Mouse Wheel` / `Scrollbar` | Smooth vertical scrolling through image rows (Grid mode) |
| **Zoom Gallery Rows** | `Ctrl` + `Mouse Wheel` / `Ctrl` + `+` / `-` | Adjust row heights in Grid mode (`Ctrl` + `0` resets zoom) |
| **Select Image** | `Left Click` on image | Select an image and display its metadata in Info Panel |
| **Context Menu** | `Right Click` on image | Open menu: Show in Info Panel or Sort by SIFT Similarity |
| **Open Full-Res View** | `Double Click` on image | Enter single-image deep zoom inspection mode |
| **Zoom in Full View** | `Mouse Wheel` | Zoom in/out at mouse cursor position |
| **Pan in Full View** | `Left Click + Drag` | Pan across zoomed image (or drag viewport box on minimap) |
| **Previous / Next Image** | `Left Arrow` / `Right Arrow` | Navigate adjacent images in single-image mode |
| **Exit Full View** | `Escape` | Return to gallery or treemap view |
| **Filter by Directory** | Click breadcrumb button | Filters current view to clicked folder (`Ctrl` + `R` resets filter) |
| **Toggle Info Panel** | `View` &rarr; `Information Panel` | Show/hide image details, EXIF, and duplicate list |
| **Toggle Minimap** | `View` &rarr; `Navigator (Minimap)` | Show/hide navigation minimap in full-res view |

---

## CLI Reference

```
Usage:
  picexplore [directory|files|globs...] [OPTIONS]

Positional Arguments:
  inputs                       Directories, image files, or glob patterns to explore

Options:
  -h, --help                   Print usage and options
  -d, --directory PATH         Directory of images to view or scan
  -g, --glob PATTERN           Glob pattern(s) of image files to view or scan
  -n, --no-recurse             Do not recurse into subdirectories
  -s, --scan                   Run batch scanner headlessly without launching GUI
  --pdf PATH                   Generate PDF gallery from database headlessly
  --db, --database PATH        Path to LMDB database file (default: ~/.cache/picexplore/cache.db)
  -l, --layout NAME            PDF Layout: justified, treemap, hierarchical-treemap (default: justified)
  -m, --metric NAME            Treemap Metric: file-size, pixel-area, duplicate-count, equal-size (default: file-size)
  --style NAME                 Treemap Render Style: thumbnails, cushion, file-type-colors (default: thumbnails)
  --paper-size NAME            Paper Size: letter, legal, tabloid, a4, a3, a2, a1 (default: letter)
  --orientation NAME           Page Orientation: portrait, landscape (default: portrait)
  --page-margin INCHES         Page margin in inches (default: 0.5)
  --dpi N                      Print resolution DPI (default: 300)
  --no-container-labels        Disable container banners/borders in hierarchical treemap
  --root-dir PATH              Root directory for hierarchical treemap
  --row-height N               Target row height in pixels for justified PDF layout (default: 150)
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
| **JPEG** | `.jpg`, `.jpeg` | `libjpeg-turbo` with DCT-domain downscaling, custom error logging, and TinyEXIF orientation |
| **PNG** | `.png` | Native `libpng` decoder with fast color space conversions |
| **WebP** | `.webp` | Native `libwebp` decoder with hardware-accelerated scaling |
| **TIFF** | `.tif`, `.tiff` | `libtiff` with RGBA converter and floating-point fallback |
| **BMP** | `.bmp` | High-performance `stb_image` decoding |
| **TGA** | `.tga` | High-performance `stb_image` decoding |

---

## Performance & Architecture

- **Unified User Content Cache (`~/.cache/picexplore/cache.db`)**: Global content-addressable LMDB database caches metadata, multi-scale thumbnails, and SIFT vectors across all viewed folders. Opening subdirectories and subsequently opening parent directories instantly reuses cached thumbnails with zero re-decoding.
- **Embedded SIFT Visual Search Engine**: Self-contained C++ implementation (derived from VLFeat) extracting 128-dimensional Root-SIFT descriptors directly from aspect-ratio thumbnails without requiring external computer vision frameworks.
- **Squarified Treemap Layout Engine**: Zero-allocation C++17 algorithm utilizing sorted area index bounds and constant-time ratio evaluation to lay out 10,000+ images in under 1 millisecond.
- **Lock-Free Multi-Core LMDB Reads**: Employs thread-local read-only transactions (`MDB_RDONLY`) without mutex locks, allowing worker threads across all CPU cores to decode thumbnails simultaneously.
- **DCT-Domain JPEG Decoding**: Uses `libjpeg-turbo`'s IDCT scaling factors (1/1, 1/2, 1/4, 1/8) to decode JPEGs directly at requested thumbnail resolutions, saving significant CPU time and memory bandwidth.
- **Dedicated Square Thumbnail Cache**: Automatically generates and stores center-cropped square thumbnails (`hash:sq128`, `hash:sq64`) in LMDB for instant treemap mosaic rendering.
- **$O(1)$ LRU Memory Management**: Visibility and memory eviction use constant-time hash set checks and incremental size tracking, eliminating UI thread latency spikes.
- **Priority Queue & Scroll Stabilization**: Prioritizes thumbnail generation for items currently visible in the viewport, waiting 500ms after scrolling stops to prevent pipeline thrashing.

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

---

## AI

I have attempted to create PicExplore a number of times over the years, with
varying degrees of success.  I started back in the day with an attempt at a
manual translation of the justified layout algorithm to C, and earlier AIs in
GitHub Copilot were able to finish/correct that and handle the command line PDF
mode.  The GUI, however, proved less tractable—I had some notions of what
architecture I wanted (LMDB, concurrentqueue for async work, FLTK for
minimalist portability, fast libjpeg thumbnail decoding), but early attempts at
using AIs to realize it weren't successful.

This effort initially succeeded by leveraging Gemini 3.1 Pro High, replaced by
Gemini 3.7 Flash High when it became available, working through iterative
testing on my part to identify bottlenecks.  More recently, Gemini 3.8 Flash
and Gemini 4 Argon have been used to tackle to continue refining the design and
implementation.
