# picexplore

Program for exploring what images are present in a filesystem.

Fast identification and display of JPEG, PNG, BMP, and TGA images with thumbnail generation and PDF gallery creation.

## GUI Features 

The FLTK-based GUI application provides an interactive way to browse image thumbnails:

### Current Features
- **Justified Layout Display**: Thumbnails arranged using optimized justified layout algorithm
- **Database Integration**: Open existing LMDB databases or scan new directories  
- **Interactive Selection**: Click thumbnails to select images
- **Scrollable View**: Mouse wheel scrolling through large image collections
- **Menu Interface**: File menu for opening databases/directories and generating PDFs
- **Progressive Loading**: Shows placeholders sized to correct aspect ratios immediately
- **Background Threading**: Async thumbnail generation with priority queues
- **Progress Indication**: Visual feedback during scanning and thumbnail generation
- **Prefetch Support**: Preload visible regions and next/previous areas during scrolling

### Controls
- **Mouse wheel**: Scroll through thumbnails
- **Left click**: Select thumbnail
- **Ctrl+D**: Open database
- **Ctrl+I**: Open directory (scans in background)
- **Ctrl+C**: Cancel directory scan
- **Ctrl+P**: Generate PDF
- **Ctrl+G**: Start background thumbnail generation
- **Ctrl+S**: Stop background thumbnail generation
- **Ctrl+Q**: Quit application

## Complete Workflow

The complete image exploration workflow is now handled by a single unified tool called `picexplore`:

### Basic Usage

```bash
# Launch GUI (default behavior):
picexplore

# Launch GUI with specific database:
picexplore --database /path/to/images.db

# Launch GUI and scan directory:
picexplore --directory /path/to/photos

# Scan directory and build/update database (no GUI):
picexplore --scan-only --directory /path/to/photos

# Generate PDF from existing database (no GUI):
picexplore --scan-only --pdf gallery.pdf

# Scan directory and generate PDF in one step (no GUI):
picexplore --scan-only --directory /path/to/photos --pdf gallery.pdf
```

This will:
- Recursively scan the specified directory for image files (JPEG, PNG, BMP, TGA)
- Compute unique content-based hashes (xxHash) for each image
- Generate multiple JPEG thumbnails at sizes: 32, 64, 128, 256, 512, 1024 px (maximum dimension)
- Store file paths, hashes, and thumbnails in an LMDB database
- Skip duplicate images (same content hash) and corrupt/unreadable files gracefully
- Generate multi-page PDF with justified layout algorithm for optimal space usage
- Create 8.5x11" pages at 300 DPI with 0.5" margins
- Display periodic status reports every 10 seconds
- Show detailed timing summary for all major phases

## Tools

### picexplore (UNIFIED)

Unified image scanner, database manager, and gallery viewer that combines scanning/PDF generation with interactive GUI browsing.

**Features:**
- Unified interface: GUI by default, scan-only mode available
- Justified layout thumbnail display with progressive loading
- Background threading for scanning and thumbnail generation
- Database integration with LMDB storage
- PDF gallery generation with customizable layout
- Interactive menu-driven interface with keyboard shortcuts

**Usage:**
```bash
picexplore [OPTIONS]
```

**Options (GUI Mode):**
- `-h, --help`: Show help message
- `-d, --database PATH`: Open LMDB database at PATH
- `-i, --directory PATH`: Open directory PATH (will scan/build database)

**Options (Scan-Only Mode):**
- `--scan-only`: Run in scan-only mode (no GUI)
- All options from the command-line scanning program (directory, database, PDF options, etc.)

**Examples:**
```bash
picexplore                           # Launch GUI
picexplore --database ./images.db    # Launch GUI with database
picexplore --directory ~/Pictures    # Launch GUI and scan directory  
picexplore --scan-only --help        # Show scan-only mode options
```

### simfind (NEW)
Content-based image similarity search tool that works with picexplore databases.

**Usage:**
```bash
simfind [OPTIONS]
```

**Options:**
- `-q, --query PATH`: Path to query image (JPEG file)
- `-d, --database PATH`: Path to picexplore LMDB database
- `-t, --threshold N`: Similarity score threshold (0.0-1.0, default: 0.7)
- `-n, --max-results N`: Maximum number of results (0 = unlimited, default: 100)
- `-m, --model PATH`: Path to ONNX model file (optional)
- `-c, --cache PATH`: Path to feature cache database (optional)
- `--no-cache`: Disable feature caching
- `-v, --verbose`: Enable verbose output

**Examples:**
```bash
# Find similar images with default threshold
simfind --query photo.jpg --database images.db

# Find highly similar images only
simfind --query photo.jpg --database images.db --threshold 0.9

# Get all similar images above threshold
simfind --query photo.jpg --database images.db --threshold 0.5 --max-results 0

# Use verbose output to see timing information
simfind --query photo.jpg --database images.db --verbose
```

**Features:**
- **Fast similarity search**: Uses largest available thumbnails (up to 1024px) for quick processing
- **Content-based features**: Currently uses color histograms and texture analysis (64 dimensions)
- **Extensible design**: Ready for ONNX Runtime integration with MobileNetV2/V3 models
- **Feature caching**: Optional LMDB-based caching of computed feature vectors
- **Efficient indexing**: Uses mlpack for fast nearest neighbor search
- **Robust output**: One file path per line, sorted by similarity (most similar first)

## Complete Examples

```bash
# Launch GUI for interactive browsing
picexplore

# Launch GUI with a specific database
picexplore --database ~/Pictures/images.db

# Launch GUI and scan a directory in the background
picexplore --directory ~/Pictures

# Scan directory and create thumbnail database (no GUI)
picexplore --scan-only --directory ~/Pictures --verbose

# Generate PDF gallery from existing thumbnails (no GUI)
picexplore --scan-only --pdf my_photo_gallery.pdf --row-height 200

# Generate PDF with custom layout padding (20px on all sides)
picexplore --scan-only --pdf gallery.pdf --layout-pad 20

# Generate PDF with asymmetric padding (larger top/bottom padding)
picexplore --scan-only --pdf gallery.pdf --layout-pad-top 30 --layout-pad-bottom 30 --layout-pad-left 10 --layout-pad-right 10

# Do both operations in one command (no GUI)
picexplore --scan-only --directory ~/Pictures --pdf my_photo_gallery.pdf --verbose

# NEW: Find similar images using content-based similarity
simfind --query ~/Pictures/vacation_photo.jpg --database images.db --threshold 0.8

# Find highly similar images and copy them to a folder
simfind --query sample.jpg --database images.db --threshold 0.9 | while read -r img; do
    cp "$img" similar_images/
done
```

## Building

### Prerequisites

You need the following system packages installed:

- CMake 3.12 or later
- A C++17 compatible compiler (GCC, Clang)
- libjpeg-turbo development headers (`libjpeg-turbo8-dev` on Ubuntu/Debian)
- pkg-config

**For GUI application (picexplore):**
- FLTK dependencies:
  - X11 development headers (`libx11-dev libxext-dev libxft-dev libxinerama-dev` on Ubuntu/Debian)
  - FontConfig development headers (`libfontconfig1-dev` on Ubuntu/Debian) 
  - OpenGL development headers (`libgl1-mesa-dev libglu1-mesa-dev` on Ubuntu/Debian)

On Ubuntu/Debian:
```bash
# Core dependencies
sudo apt-get install cmake build-essential libjpeg-turbo8-dev libexif-dev pkg-config

# Additional GUI dependencies
sudo apt-get install libx11-dev libxext-dev libxft-dev libxinerama-dev libfontconfig1-dev libgl1-mesa-dev libglu1-mesa-dev

**For similarity search features (optional):**
- mlpack development headers (`libmlpack-dev` on Ubuntu/Debian)
- ensmallen development headers (`libensmallen-dev` on Ubuntu/Debian)
```

On Ubuntu/Debian:
```bash
# Required packages
sudo apt-get install cmake build-essential libjpeg-turbo8-dev pkg-config

# Optional packages for similarity search
sudo apt-get install libmlpack-dev libensmallen-dev
```

### Build Steps

1. Clone the repository with submodules:
```bash
git clone --recursive https://github.com/starseeker/picexplore.git
cd picexplore
```

If you already cloned without submodules, initialize them:
```bash
git submodule update --init --recursive
```

2. Build the project:
```bash
mkdir build
cd build
cmake ..
make
```

This will build the executable(s):
- `picexplore` (unified image scanner, database manager, and gallery viewer)
- `simfind` (image similarity search tool - only if mlpack is available)

## Supported Image Formats

- **JPEG** (.jpg, .jpeg) - Efficient DCT-domain downscaling during decode for optimal thumbnail generation
- **PNG** (.png) - Full support with automatic JPEG thumbnail conversion
- **BMP** (.bmp) - Full support with automatic JPEG thumbnail conversion  
- **TGA** (.tga) - Full support with automatic JPEG thumbnail conversion

All formats support grayscale, RGB, and RGBA color modes.

## Image Processing Details

The application uses an optimized pipeline designed for efficiency and quality:

### JPEG Processing (Optimized)
1. **EXIF Orientation Reading**: Uses TinyEXIF to read EXIF orientation data from JPEG files
2. **Orientation Correction**: Applies appropriate rotations and flips to ensure correct thumbnail orientation
3. **DCT-Domain Downscaling**: JPEGs are decoded using libjpeg-turbo with DCT-domain scaling (scale factors 1/1, 1/2, 1/4, 1/8)
4. **Scale Factor Grouping**: Thumbnails are grouped by optimal scale factor to minimize decode operations
5. **Single Decode Per Group**: Each scale factor group requires only one decode operation
6. **JPEG Encoding**: All thumbnails are stored as JPEG with 90% quality for optimal size/quality balance

### Non-JPEG Processing  
1. **EXIF Orientation Reading**: Uses TinyEXIF to read EXIF orientation data where available
2. **Orientation Correction**: Applies appropriate rotations and flips to ensure correct thumbnail orientation
3. **Full Resolution Decoding**: PNG, BMP, TGA images are decoded at their original resolution using stb_image
4. **High-Quality Resizing**: Thumbnails are generated using stb_image_resize with linear interpolation
5. **JPEG Encoding**: All thumbnails are stored as JPEG with 90% quality for optimal size/quality balance

This approach optimizes performance and memory usage for JPEG files (which typically represent the majority of images in photo collections) while maintaining high quality for all supported formats.

## Performance Notes

- **Fast scanning**: Optimized for processing large image collections
- **Content-based deduplication**: Identical images (by content) are processed only once
- **Efficient JPEG processing**: DCT-domain downscaling minimizes memory usage and decode time
- **Scale factor optimization**: Groups thumbnails by scale factor to minimize JPEG decode operations
- **Lightning-fast database**: LMDB provides high-performance storage and retrieval
- **Memory efficient**: Processes images one at a time with proper cleanup
- **Status reporting**: Real-time progress updates every 10 seconds
- **Performance instrumentation**: Detailed timing breakdown of all processing phases

