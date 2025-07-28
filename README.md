# picexplore

Program for exploring what images are present in a filesystem.

Fast identification and display of JPEG, PNG, BMP, and TGA images with thumbnail generation and PDF gallery creation.

## Complete Workflow

The complete image exploration workflow is now handled by a single unified tool called `picscan`:

### Basic Usage

```bash
# Scan directory and build/update database:
picscan --directory /path/to/photos

# Generate PDF from existing database:
picscan --pdf gallery.pdf

# Scan directory and generate PDF in one step:
picscan --directory /path/to/photos --pdf gallery.pdf
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

### picscan
Unified image scanner and PDF gallery generator that combines the functionality of the previous separate tools.

**Usage:**
```bash
picscan [OPTIONS]
```

**Options:**
- `-h, --help`: Show help message
- `-d, --directory PATH`: Directory to scan for images
- `--db PATH`: LMDB database path (default: `./images.db`)
- `--pdf PATH`: PDF output file path
- `--row-height N`: Target row height in pixels for PDF layout (default: 150)
- `--margin N`: Spacing between images in pixels for PDF layout (default: 10)
- `--layout-pad N`: Layout padding for all sides in pixels (default: 0)
- `--layout-pad-top N`: Layout padding top in pixels
- `--layout-pad-bottom N`: Layout padding bottom in pixels  
- `--layout-pad-left N`: Layout padding left in pixels
- `--layout-pad-right N`: Layout padding right in pixels
- `-v, --verbose`: Enable verbose output with detailed information

**PDF Layout Controls:**
- **Page margins**: Fixed 0.5" margins from page edge (controls distance from paper edge)
- **Layout padding**: Internal padding around the grid of images (`--layout-pad-*` options)
- **Image spacing**: Space between individual images within the grid (`--margin` option)

**Features:**
- **Multi-format support**: JPEG, PNG, BMP, TGA
- **Efficient JPEG thumbnailing**: Uses DCT-domain downscaling during decode for optimal performance and memory usage
- **EXIF orientation support**: Automatically reads and applies EXIF orientation data for correct thumbnail display using TinyEXIF
- **Content-based deduplication**: Prevents duplicate processing using xxHash
- **Multiple thumbnail sizes**: 32, 64, 128, 256, 512, 1024 px maximum dimension
- **Robust error handling**: Gracefully skips corrupt or unreadable images
- **LMDB database**: Lightning-fast storage and retrieval
- **Flexible operation**: Can scan only, generate PDF only, or both in one command
- **Status reporting**: Periodic updates every 10 seconds during processing
- **Performance instrumentation**: Detailed timing information for all major phases
- **Justified layout**: Uses optimized layout algorithm for space-efficient PDF pages

## Complete Examples

```bash
# Scan your photo directory and create thumbnail database
picscan --directory ~/Pictures --verbose

# Generate PDF gallery from existing thumbnails  
picscan --pdf my_photo_gallery.pdf --row-height 200

# Generate PDF with custom layout padding (20px on all sides)
picscan --pdf gallery.pdf --layout-pad 20

# Generate PDF with asymmetric padding (larger top/bottom padding)
picscan --pdf gallery.pdf --layout-pad-top 30 --layout-pad-bottom 30 --layout-pad-left 10 --layout-pad-right 10

# Do both operations in one command
picscan --directory ~/Pictures --pdf my_photo_gallery.pdf --verbose

# The PDF gallery is now ready to view!
```

## Building

### Prerequisites

You need the following system packages installed:

- CMake 3.12 or later
- A C++17 compatible compiler (GCC, Clang)
- libjpeg-turbo development headers (`libjpeg-turbo8-dev` on Ubuntu/Debian)
- pkg-config

On Ubuntu/Debian:
```bash
sudo apt-get install cmake build-essential libjpeg-turbo8-dev pkg-config
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

This will build the unified executable:
- `picscan` (unified image scanner and PDF gallery generator)

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

