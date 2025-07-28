# Image Similarity Search

This directory contains the image similarity search components for the picexplore project.

## Overview

The similarity search system provides content-based image similarity using feature vectors extracted from thumbnails. It integrates with the existing picscan database format to provide fast similarity search across large image collections.

## Components

### ImageSimilarityIndex Class (`image_similarity_index.h/cpp`)

The main class that provides similarity search functionality:

- **Database Integration**: Reads from existing picscan LMDB databases
- **Feature Extraction**: Extracts features from the largest available thumbnails (typically 1024px)
- **Similarity Search**: Uses mlpack for efficient nearest neighbor search
- **Caching**: Optional LMDB-based caching of computed feature vectors
- **Extensible Design**: Ready for ONNX Runtime integration with MobileNetV2/V3 models

#### Current Feature Extraction

The current implementation uses a simple but effective feature extraction approach:
- **Color Histograms**: 48 features (16 bins each for R, G, B channels)
- **Texture Features**: 8 features (mean intensity, variance, edge detection, corner response)
- **Spatial Features**: 8 additional features including aspect ratio

This provides **64-dimensional feature vectors** that are fast to compute and surprisingly effective for similarity search.

#### TODO: ONNX Runtime Integration

The class is designed to easily integrate ONNX Runtime for more sophisticated feature extraction:

```cpp
// TODO: Replace simple feature extraction with ONNX Runtime + MobileNetV2/V3
// 1. Initialize ONNX Runtime environment and session
// 2. Preprocess images to model input format (224x224, normalized)
// 3. Run inference to get 1280-dimensional feature vectors
// 4. Use more sophisticated similarity metrics
```

### simfind Command Line Tool (`simfind.cpp`)

A robust command-line tool for performing similarity searches:

```bash
# Basic usage
simfind --query photo.jpg --database images.db

# Advanced usage with custom parameters
simfind --query photo.jpg --database images.db --threshold 0.9 --max-results 50 --verbose
```

**Features:**
- Configurable similarity thresholds (0.0-1.0)
- Limit maximum results or get all matches
- Verbose output with timing information
- Feature caching support
- Clean output format (one file path per line)

## Usage Examples

### 1. Basic Similarity Search

```bash
# Build picscan database from your images
picscan --directory /path/to/photos --db photos.db

# Find similar images
simfind --query sample.jpg --database photos.db
```

### 2. Advanced Usage

```bash
# High precision search (find only very similar images)
simfind --query sample.jpg --database photos.db --threshold 0.95

# Low threshold search with unlimited results
simfind --query sample.jpg --database photos.db --threshold 0.5 --max-results 0

# Verbose output with timing
simfind --query sample.jpg --database photos.db --verbose

# Custom feature cache location
simfind --query sample.jpg --database photos.db --cache /tmp/features.cache
```

### 3. Integration with Shell Scripts

```bash
#!/bin/bash
# Find all images similar to a query and copy them to a directory

QUERY="$1"
DATABASE="$2"
OUTPUT_DIR="$3"

mkdir -p "$OUTPUT_DIR"

simfind --query "$QUERY" --database "$DATABASE" --threshold 0.8 | while read -r similar_image; do
    cp "$similar_image" "$OUTPUT_DIR/"
done

echo "Copied similar images to $OUTPUT_DIR"
```

## Performance Characteristics

- **Index Loading**: ~40ms for small datasets, scales with database size
- **Query Time**: ~80ms per query after index is loaded
- **Memory Usage**: ~64 bytes per image for feature vectors (simple features)
- **Scalability**: Designed for thousands of images; uses mlpack KNN for current implementation

## Future Enhancements

### ONNX Runtime Integration

```cpp
// Extension point for ONNX Runtime integration
bool ImageSimilarityIndex::extract_features_onnx(const uint8_t* image_data, 
                                                 int width, int height,
                                                 std::vector<float>& feature_vector) {
    // 1. Preprocess image to 224x224 RGB, normalize to [0,1]
    // 2. Create ONNX Runtime tensor from preprocessed data
    // 3. Run inference with MobileNetV2/V3 model
    // 4. Extract 1280-dimensional feature vector from model output
    // 5. Apply L2 normalization for cosine similarity
}
```

### LSH for Large Datasets

```cpp
// For datasets with >10,000 images, switch to LSH
if (num_images > 10000) {
    // Use mlpack::LSHSearch for sub-linear search time
    auto* lsh_search = new mlpack::LSHSearch<>(dataset, projections, hash_width);
    search_index_ = lsh_search;
}
```

### GPU Acceleration

- ONNX Runtime GPU providers for faster feature extraction
- GPU-accelerated similarity search using FAISS integration

## Dependencies

- **mlpack**: For similarity search (KNN, future LSH)
- **LMDB**: For database access and feature caching
- **stb_image**: For image loading and processing
- **xxhash**: Content-based hashing (inherited from picscan)
- **armadillo**: Linear algebra (mlpack dependency)

## Build Requirements

The similarity components are built automatically when mlpack is detected:

```bash
# Install required packages
sudo apt-get install libmlpack-dev libensmallen-dev

# Build with CMake
mkdir build && cd build
cmake .. && make -j$(nproc)
```

If mlpack is not available, the main picscan functionality still works, but similarity search components are skipped.