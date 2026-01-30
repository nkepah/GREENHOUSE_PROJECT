#!/bin/bash
#
# 🔧 Update Frontend Files Only
#
# Quick script to update just the frontend files without full reinstall.
# Useful when you've made changes to the HTML/CSS/JS.
#
# Usage:
#   ./update-frontend.sh nkepah@100.92.151.67
#

set -e

# Default values
DEFAULT_PI_HOST="nkepah@100.92.151.67"

if [ -z "$1" ]; then
    echo "No host specified, using default: $DEFAULT_PI_HOST"
    PI_HOST="$DEFAULT_PI_HOST"
else
    PI_HOST="$1"
fi
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/greenhouse-proxy"

echo "🔄 Updating frontend files on $PI_HOST"

# Check for local public folder first
if [ -d "$SCRIPT_DIR/public" ] && [ "$(ls -A $SCRIPT_DIR/public 2>/dev/null)" ]; then
    SOURCE="$SCRIPT_DIR/public"
# Fall back to ESP32 data folder
elif [ -d "$SCRIPT_DIR/../data" ]; then
    SOURCE="$SCRIPT_DIR/../data"
else
    echo "❌ No frontend files found!"
    echo "   Check: $SCRIPT_DIR/public or $SCRIPT_DIR/../data"
    exit 1
fi

echo "Source: $SOURCE"

# Create temp directory and copy files
TMP_DIR="/tmp/greenhouse-frontend-$$"
mkdir -p "$TMP_DIR"

cp "$SOURCE"/*.html "$TMP_DIR/" 2>/dev/null || true
cp "$SOURCE"/*.html.gz "$TMP_DIR/" 2>/dev/null || true
cp "$SOURCE"/*.css "$TMP_DIR/" 2>/dev/null || true
cp "$SOURCE"/*.js "$TMP_DIR/" 2>/dev/null || true
cp "$SOURCE"/*.png "$TMP_DIR/" 2>/dev/null || true
cp "$SOURCE"/*.ico "$TMP_DIR/" 2>/dev/null || true

# Decompress gzipped files
for gz in "$TMP_DIR"/*.gz; do
    [ -f "$gz" ] && gunzip -kf "$gz" 2>/dev/null || true
done

# Copy to Pi
echo "Copying files to Pi..."
scp "$TMP_DIR"/* "$PI_HOST:$INSTALL_DIR/public/" 2>/dev/null || \
    ssh "$PI_HOST" "sudo mkdir -p $INSTALL_DIR/public && sudo chown nkepah:nkepah $INSTALL_DIR/public" && \
    scp "$TMP_DIR"/* "$PI_HOST:$INSTALL_DIR/public/"

# Cleanup
rm -rf "$TMP_DIR"

echo ""
echo "✅ Frontend updated!"
echo "   No restart needed - changes are immediate."
