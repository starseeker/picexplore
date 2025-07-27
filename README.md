# picexplore

Program for exploring what images are present in a filesystem.

Initial goal is a fast identification of and display of all JPEG
images.

## Tools

### image_thumb_db
Scans directories for images and creates LMDB database with thumbnails.
See [README_image_thumb_db.md](README_image_thumb_db.md) for details.

### thumb_gallery_pdf
Generates justified-layout PDF image galleries from LMDB thumbnails.

**Usage:**
```bash
thumb_gallery_pdf --lmdb /path/to/images.db --output gallery.pdf [options]
```

**Options:**
- `--lmdb PATH`: Input LMDB database path (required)
- `--output PATH`: Output PDF file path (required)
- `--row-height N`: Target row height in pixels (default: 150)
- `--margin N`: Layout margin between images in pixels (default: 10)

**Features:**
- Reads thumbnails from LMDB database created by `image_thumb_db`
- Uses justified layout algorithm for optimal space usage
- Creates 8.5x11" pages at 300 DPI with 0.5" margins
- Automatically scales images to fit layout boxes
- Generates multi-page PDF with all images sorted alphabetically by path

**Example:**
```bash
# First create thumbnail database
image_thumb_db --directory /photos --output photos.db

# Then generate PDF gallery
thumb_gallery_pdf --lmdb photos.db --output photo_gallery.pdf --row-height 200
```

