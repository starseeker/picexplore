#!/bin/bash

# Demo script showing how to use the picexplore logging system
# Save this as demo_logging.sh and run it to see different logging scenarios

echo "=== Picexplore Logging System Usage Examples ==="
echo ""
echo "This script demonstrates how to control logging in picexplore"
echo "using environment variables."
echo ""

echo "1. Running picexplore with no debug output (default):"
echo "   ./picexplore /path/to/images"
echo ""

echo "2. Enable basic logging for all categories:"
echo "   PICEXPLORE_LOGGING=1 ./picexplore /path/to/images"
echo ""

echo "3. Enable verbose logging for all categories:"
echo "   PICEXPLORE_LOGGING=2 ./picexplore /path/to/images"
echo ""

echo "4. Enable only batch processing debugging:"
echo "   BATCH_LOGGING=2 ./picexplore /path/to/images"
echo ""

echo "5. Debug layout and UI issues:"
echo "   UI_LOGGING=2 ./picexplore /path/to/images"
echo ""

echo "6. Debug thumbnail generation and thread management:"
echo "   THREAD_LOGGING=2 ./picexplore /path/to/images"
echo ""

echo "7. Debug directory scanning:"
echo "   SCAN_LOGGING=2 ./picexplore /path/to/images"
echo ""

echo "8. Mixed debugging (batch verbose, UI basic, threads off):"
echo "   BATCH_LOGGING=2 UI_LOGGING=1 THREAD_LOGGING=0 ./picexplore /path/to/images"
echo ""

echo "9. Redirect debug output to a file for analysis:"
echo "   BATCH_LOGGING=2 ./picexplore /path/to/images 2> batch_debug.log"
echo ""

echo "10. Filter specific category output:"
echo "    PICEXPLORE_LOGGING=2 ./picexplore /path/to/images 2>&1 | grep 'BATCH:'"
echo ""

echo "=== Environment Variables ==="
echo "PICEXPLORE_LOGGING  - Global logging level (0=off, 1=basic, 2=verbose)"
echo "BATCH_LOGGING       - Batch processing logging level"  
echo "UI_LOGGING          - UI and layout logging level"
echo "THREAD_LOGGING      - Thread management logging level"
echo "SCAN_LOGGING        - Directory scanning logging level"
echo ""

echo "=== Log Output Format ==="
echo "[HH:MM:SS.mmm] [CATEGORY:LEVEL] message"
echo ""
echo "Examples:"
echo "[14:32:15.123] [BATCH:1] Processing batch of 25 images"
echo "[14:32:15.145] [BATCH:2] Added image to batch, pending count: 1"  
echo "[14:32:15.156] [UI:1] Recalculating layout for batch of 25 images"
echo "[14:32:15.201] [THREAD:1] ThreadManager available, queuing thumbnail requests"
echo ""

echo "For more information, see docs/Logging.md"