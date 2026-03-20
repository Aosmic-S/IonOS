#!/bin/bash
# IonOS Flash Helper
set -e
PORT=${1:-/dev/ttyUSB0}
BAUD=${2:-460800}

echo "╔════════════════════════════╗"
echo "║   IonOS Flash & Monitor    ║"
echo "╚════════════════════════════╝"
echo "Port: $PORT  Baud: $BAUD"
echo ""

# Check IDF
if [ -z "$IDF_PATH" ]; then
    echo "ERROR: IDF_PATH not set. Run: source ~/esp/esp-idf/export.sh"
    exit 1
fi

# Generate assets first
echo "Generating assets..."
python3 tools/gen_assets.py

# Build
echo "Building IonOS..."
idf.py build

# Flash
echo "Flashing to $PORT..."
idf.py -p "$PORT" -b "$BAUD" flash

# Monitor
echo "Starting monitor (Ctrl+] to exit)..."
idf.py -p "$PORT" monitor
