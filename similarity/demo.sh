#!/bin/bash
# demo.sh - Demonstration script for picexplore similarity search

set -e

echo "=== PicExplore Similarity Search Demo ==="
echo

# Check if tools exist
if [ ! -f "./picscan" ] || [ ! -f "./simfind" ]; then
    echo "Error: Please run this script from the build directory"
    echo "Make sure both picscan and simfind are built"
    exit 1
fi

# Check if test images exist
if [ ! -d "../test_images" ]; then
    echo "Error: test_images directory not found"
    exit 1
fi

echo "Step 1: Scanning test images and creating database..."
./picscan --directory ../test_images --db demo.db --verbose
echo

echo "Step 2: Finding similar images to test.jpg..."
echo "Query image: ../test_images/test.jpg"
echo "Results:"
./simfind --query ../test_images/test.jpg --database demo.db --threshold 0.5 --verbose
echo

echo "Step 3: Finding similar images to the PNG file..."
echo "Query image: ../test_images/sdf_test_arial_16.png"
echo "Results:"
./simfind --query ../test_images/sdf_test_arial_16.png --database demo.db --threshold 0.8 --verbose
echo

echo "Step 4: Testing different similarity thresholds..."
echo "High threshold (0.99) - very similar only:"
./simfind --query ../test_images/test.jpg --database demo.db --threshold 0.99
echo

echo "Low threshold (0.9) - more permissive:"
./simfind --query ../test_images/test.jpg --database demo.db --threshold 0.9
echo

echo "Demo completed successfully!"
echo
echo "The similarity search system is working. Key features:"
echo "- Uses largest available thumbnails (up to 1024px) for fast processing"
echo "- Extracts 64-dimensional feature vectors (color + texture)"
echo "- Provides similarity scores from 0.0 (different) to 1.0 (identical)"
echo "- Uses mlpack for efficient nearest neighbor search"
echo "- Ready for ONNX Runtime integration (see similarity/ONNX_INTEGRATION.md)"
echo

# Cleanup
rm -f demo.db demo.db-lock
echo "Cleaned up demo database files."