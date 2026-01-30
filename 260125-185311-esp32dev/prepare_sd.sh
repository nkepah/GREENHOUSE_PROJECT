#!/bin/bash
# Script to prepare SD card for greenhouse system

echo "========================================="
echo "Greenhouse SD Card Setup Script"
echo "========================================="
echo ""

# Check if SD card path is provided
if [ -z "$1" ]; then
    echo "Usage: ./prepare_sd.sh /path/to/sdcard"
    echo ""
    echo "Example:"
    echo "  Windows Git Bash: ./prepare_sd.sh /d"
    echo "  Mac/Linux:        ./prepare_sd.sh /Volumes/SD_CARD"
    exit 1
fi

SD_PATH="$1"

# Check if path exists
if [ ! -d "$SD_PATH" ]; then
    echo "Error: Path $SD_PATH does not exist!"
    echo "Make sure SD card is mounted and path is correct."
    exit 1
fi

echo "Target SD Card: $SD_PATH"
echo ""

# Create directory structure
echo "Creating directories..."
mkdir -p "$SD_PATH/logs"
mkdir -p "$SD_PATH/images"
mkdir -p "$SD_PATH/backups"
mkdir -p "$SD_PATH/data"
echo "✓ Directories created"
echo ""

# Copy icons.png if it exists
if [ -f "data/icons.png" ]; then
    echo "Copying icons.png (1.3MB) to SD card..."
    cp data/icons.png "$SD_PATH/"
    echo "✓ icons.png copied"
    echo ""
    echo "⚠️  IMPORTANT: After testing SD card works,"
    echo "   you can delete icons.png from data/ folder"
    echo "   to free up 1.3MB in ESP32 flash!"
else
    echo "⚠️  icons.png not found in data/ folder"
    echo "   File will be served from LittleFS"
fi

echo ""
echo "========================================="
echo "SD Card Setup Complete!"
echo "========================================="
echo ""
echo "Next steps:"
echo "1. Safely eject SD card"
echo "2. Insert into SD card module"
echo "3. Connect module to ESP32 (see SD_CARD_SETUP.md)"
echo "4. Upload code: platformio run --target upload"
echo "5. Check serial monitor for [SD] messages"
echo ""
