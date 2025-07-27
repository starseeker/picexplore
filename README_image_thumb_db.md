# Image Thumb DB - Standalone Image Scanner/Thumbnailer/Database Processor

## Overview

`image_thumb_db` is a standalone executable that scans directories for image files, generates thumbnails, and stores metadata in a Lightning Memory-Mapped Database (LMDB). It's designed for fast image content identification and thumbnail generation.

## Dependencies

This project uses the following third-party libraries as git submodules:

- **xxHash**: Fast hashing library for image content identification
- **stb**: Single-file public domain libraries for image loading  
- **epeg**: JPEG thumbnailing library for efficient thumbnail generation
- **LMDB**: Lightning Memory-Mapped Database for fast metadata storage
- **cxxopts**: Modern C++ command line parsing library

## Building

### Prerequisites

You need the following system packages installed:

- CMake 3.12 or later
- A C++17 compatible compiler (GCC, Clang)
- libjpeg development headers (`libjpeg-dev` on Ubuntu/Debian)
- libexif development headers (`libexif-dev` on Ubuntu/Debian)
- pkg-config

On Ubuntu/Debian:
```bash
sudo apt-get install cmake build-essential libjpeg-dev libexif-dev pkg-config
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

This will build both executables:
- `justified_layout_test` (original executable)
- `image_thumb_db` (new image scanner)

## Usage

### Basic Usage

Scan the current directory for images:
```bash
./image_thumb_db
```

### Options

- `-h, --help`: Show help message
- `-d, --directory <path>`: Directory to scan for images (default: current directory)
- `-o, --output <path>`: Output database path (default: `./images.db`)
- `-v, --verbose`: Enable verbose output with detailed information

### Examples

Scan a specific directory with verbose output:
```bash
./image_thumb_db -d /path/to/photos -v
```

Specify custom database location:
```bash
./image_thumb_db -d /path/to/photos -o /path/to/custom.db
```

## Supported Image Formats

The scanner supports the following image formats:
- JPEG (.jpg, .jpeg)
- PNG (.png)
- BMP (.bmp)
- TGA (.tga)

## Technical Details

- Uses xxHash for fast content-based hashing of image data
- Leverages stb_image for cross-format image loading
- Employs LMDB for efficient metadata storage and retrieval
- Integrates epeg for optimized JPEG thumbnail generation
- Built with modern C++17 features and practices

## Database Schema

The LMDB database stores image metadata including:
- File paths and names
- Image dimensions and channel information
- Content hashes for duplicate detection
- Thumbnail data (planned)
- EXIF metadata (planned)

## Development Status

This is currently a basic implementation that demonstrates:
- ✅ Command line argument parsing
- ✅ Recursive directory scanning
- ✅ Image format detection and loading
- ✅ Content hashing with xxHash
- ✅ Basic LMDB database operations
- 🔄 Thumbnail generation (framework ready)
- 🔄 Complete metadata storage
- 🔄 Duplicate image detection

## License

This software is provided under the MIT License. See individual dependency licenses for third-party components.