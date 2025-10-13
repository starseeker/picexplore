#!/bin/bash
# Test script for PicExplore GUI

echo "PicExplore GUI Test Script"
echo "=========================="
echo ""

# Check if executable exists
if [ ! -f "../build/gui/picexplore_gui" ]; then
    echo "ERROR: picexplore_gui not built. Run 'make picexplore_gui' first."
    exit 1
fi

# Check if test images exist
if [ ! -d "../test_images" ]; then
    echo "ERROR: test_images directory not found"
    exit 1
fi

echo "Testing GUI with test_images directory..."
echo ""
echo "Note: This requires a display. If you're running headless, set DISPLAY appropriately."
echo ""
echo "Running: ./picexplore_gui ../test_images"
echo ""

# Run the GUI (will fail if no display available)
cd ../build/gui
./picexplore_gui ../../test_images

echo ""
echo "Test complete."
