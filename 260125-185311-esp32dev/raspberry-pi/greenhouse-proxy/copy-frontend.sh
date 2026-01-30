#!/bin/bash
#
# 🌿 Copy Frontend Files from ESP32 Data Folder to Pi Public Folder
#
# This script copies and decompresses the HTML files from the ESP32's
# data folder to the Pi's public folder for serving.
#
# Usage:
#   ./copy-frontend.sh [source_path] [dest_path]
#
# Examples:
#   ./copy-frontend.sh                          # Uses default paths
#   ./copy-frontend.sh /home/pi/esp32-data      # Custom source
#

set -e

# Default paths
SOURCE="${1:-../data}"
DEST="${2:-./public}"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${BLUE}[INFO]${NC} Copying frontend files..."
echo "  Source: $SOURCE"
echo "  Destination: $DEST"

# Create destination if needed
mkdir -p "$DEST"

# Copy HTML files
if [ -d "$SOURCE" ]; then
    # Copy all HTML files
    for file in "$SOURCE"/*.html; do
        if [ -f "$file" ]; then
            cp "$file" "$DEST/"
            echo -e "${GREEN}[✓]${NC} Copied: $(basename "$file")"
        fi
    done
    
    # Copy and decompress gzipped files
    for file in "$SOURCE"/*.html.gz; do
        if [ -f "$file" ]; then
            cp "$file" "$DEST/"
            # Decompress to create non-gzipped version
            gunzip -kf "$DEST/$(basename "$file")" 2>/dev/null || true
            echo -e "${GREEN}[✓]${NC} Copied & decompressed: $(basename "$file")"
        fi
    done
    
    # Copy any CSS/JS files
    for file in "$SOURCE"/*.css "$SOURCE"/*.js; do
        if [ -f "$file" ]; then
            cp "$file" "$DEST/"
            echo -e "${GREEN}[✓]${NC} Copied: $(basename "$file")"
        fi
    done
    
    # Copy images
    for file in "$SOURCE"/*.png "$SOURCE"/*.jpg "$SOURCE"/*.svg "$SOURCE"/*.ico; do
        if [ -f "$file" ]; then
            cp "$file" "$DEST/"
            echo -e "${GREEN}[✓]${NC} Copied: $(basename "$file")"
        fi
    done
    
    echo ""
    echo -e "${GREEN}Frontend files copied successfully!${NC}"
    echo ""
    echo "Files in $DEST:"
    ls -la "$DEST"
else
    echo -e "${YELLOW}[⚠]${NC} Source directory not found: $SOURCE"
    echo ""
    echo "Please provide the path to your ESP32's data folder."
    echo "This is typically where index.html, routines.html, alerts.html are located."
    echo ""
    echo "Usage: ./copy-frontend.sh /path/to/esp32/data"
    exit 1
fi
