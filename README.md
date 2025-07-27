# picexplore

Program for exploring what images are present in a filesystem.

Fast identification and display of JPEG, PNG, BMP, and TGA images with thumbnail generation and PDF gallery creation.

## Complete Workflow

The complete image exploration workflow consists of two main steps:

1. **Scan directory and create thumbnail database** using `image_thumb_db`
2. **Generate PDF gallery from thumbnails** using `thumb_gallery_pdf`

### Step 1: Scan Images and Create Database

```bash
image_thumb_db --directory /path/to/photos --output photos.db --verbose
```

This will:
- Recursively scan the specified directory for image files (JPEG, PNG, BMP, TGA)
- Compute unique content-based hashes (xxHash) for each image
- Generate multiple JPEG thumbnails at sizes: 32, 64, 128, 256, 512, 1024 px (maximum dimension)
- Store file paths, hashes, and thumbnails in an LMDB database
- Skip duplicate images (same content hash) and corrupt/unreadable files gracefully

### Step 2: Generate PDF Gallery

```bash
thumb_gallery_pdf --lmdb photos.db --output photo_gallery.pdf --row-height 200
```

This will:
- Read thumbnails from the LMDB database
- Use justified layout algorithm for optimal space usage
- Create 8.5x11" pages at 300 DPI with 0.5" margins
- Generate multi-page PDF with all images sorted alphabetically by path

## Tools

### image_thumb_db
Scans directories for images and creates LMDB database with thumbnails.
See [README_image_thumb_db.md](README_image_thumb_db.md) for details.

**Usage:**
```bash
image_thumb_db [OPTIONS]
```

**Options:**
- `-h, --help`: Show help message
- `-d, --directory PATH`: Directory to scan for images (default: current directory)
- `-o, --output PATH`: Output database path (default: `./images.db`)
- `-v, --verbose`: Enable verbose output with detailed information

**Features:**
- **Multi-format support**: JPEG, PNG, BMP, TGA
- **Efficient thumbnailing**: Uses epeg for JPEG sources, stb_image for others
- **Content-based deduplication**: Prevents duplicate processing using xxHash
- **Multiple thumbnail sizes**: 32, 64, 128, 256, 512, 1024 px maximum dimension
- **Robust error handling**: Gracefully skips corrupt or unreadable images
- **LMDB database**: Lightning-fast storage and retrieval

### thumb_gallery_pdf
Generates justified-layout PDF image galleries from LMDB thumbnails.

**Usage:**
```bash
thumb_gallery_pdf --lmdb /path/to/images.db --output gallery.pdf [options]
```

**Options:**
- `--lmdb PATH`: Input LMDB database path (required)
- `-o, --output PATH`: Output PDF file path (required)
- `--row-height N`: Target row height in pixels (default: 150)
- `--margin N`: Layout margin between images in pixels (default: 10)

**Features:**
- Reads thumbnails from LMDB database created by `image_thumb_db`
- Uses justified layout algorithm for optimal space usage
- Creates 8.5x11" pages at 300 DPI with 0.5" margins
- Automatically scales images to fit layout boxes
- Generates multi-page PDF with all images sorted alphabetically by path

## Complete Example

```bash
# Step 1: Scan your photo directory and create thumbnail database
image_thumb_db --directory ~/Pictures --output my_photos.db --verbose

# Step 2: Generate PDF gallery from thumbnails
thumb_gallery_pdf --lmdb my_photos.db --output my_photo_gallery.pdf --row-height 200

# The PDF gallery is now ready to view!
```

## Building

### Prerequisites

The project is now **fully self-contained** with all C/C++ dependencies included as git submodules. You only need:

- CMake 3.12 or later
- A C++17 compatible compiler (GCC, Clang, MSVC)

**No external libraries are required!** All dependencies including libjpeg-turbo, libexif, xxHash, stb, epeg, LMDB, cxxopts, and struetype are bundled as submodules.

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

This will build both executables:
- `image_thumb_db` (image scanner and thumbnailer)
- `thumb_gallery_pdf` (PDF gallery generator)

### Advanced Configuration

For advanced users who prefer to use system-installed libraries, you can enable them with CMake options:

```bash
cmake -DUSE_SYSTEM_JPEG=ON -DUSE_SYSTEM_EXIF=ON ..
```

When using system libraries, you'll need to install the development headers:
- On Ubuntu/Debian: `sudo apt-get install libjpeg-dev libexif-dev pkg-config`
- On CentOS/RHEL: `sudo yum install libjpeg-turbo-devel libexif-devel pkgconfig`
- On macOS: `brew install jpeg libexif pkg-config`

## Supported Image Formats

- **JPEG** (.jpg, .jpeg) - Native support with epeg for efficient thumbnailing
- **PNG** (.png) - Full support with automatic JPEG thumbnail conversion
- **BMP** (.bmp) - Full support with automatic JPEG thumbnail conversion  
- **TGA** (.tga) - Full support with automatic JPEG thumbnail conversion

All formats support grayscale, RGB, and RGBA color modes.

## Performance Notes

- **Fast scanning**: Optimized for processing large image collections
- **Content-based deduplication**: Identical images (by content) are processed only once
- **Efficient thumbnailing**: JPEG sources use epeg for fast downsampling
- **Lightning-fast database**: LMDB provides high-performance storage and retrieval
- **Memory efficient**: Processes images one at a time with proper cleanup

