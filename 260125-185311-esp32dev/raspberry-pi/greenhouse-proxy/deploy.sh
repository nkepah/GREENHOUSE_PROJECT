#!/bin/bash
#
# 🌿 Quick Deploy Script
#
# One-liner deployment to Raspberry Pi via SSH.
# Copies files and runs installation remotely.
#
# Usage:
#   ./deploy.sh nkepah@100.92.151.67
#   ./deploy.sh nkepah@100.92.151.67 --esp32-ip=10.0.0.163
#

set -e

# Default values
DEFAULT_PI_HOST="nkepah@100.92.151.67"
DEFAULT_ESP32_IP="10.0.0.163"

if [ -z "$1" ]; then
    echo "Usage: ./deploy.sh [user@pi-hostname] [install options]"
    echo ""
    echo "Examples:"
    echo "  ./deploy.sh                                    # Uses default: $DEFAULT_PI_HOST"
    echo "  ./deploy.sh nkepah@100.92.151.67"
    echo "  ./deploy.sh nkepah@100.92.151.67 --esp32-ip=10.0.0.163 --with-nginx"
    echo ""
    echo "Using defaults: PI=$DEFAULT_PI_HOST, ESP32=$DEFAULT_ESP32_IP"
    PI_HOST="$DEFAULT_PI_HOST"
    INSTALL_ARGS="--esp32-ip=$DEFAULT_ESP32_IP"
else
    PI_HOST="$1"
    shift
    INSTALL_ARGS="${@:---esp32-ip=$DEFAULT_ESP32_IP}"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE_TEMP="/tmp/greenhouse-proxy-deploy"

echo "🌿 Deploying Greenhouse Proxy to $PI_HOST"
echo ""

# Create tarball of necessary files
echo "[1/4] Packaging files..."
cd "$SCRIPT_DIR"
tar -czf /tmp/greenhouse-proxy.tar.gz \
    server.js \
    package.json \
    config.json \
    greenhouse-proxy.service \
    install.sh \
    copy-frontend.sh \
    public/ 2>/dev/null || tar -czf /tmp/greenhouse-proxy.tar.gz \
    server.js \
    package.json \
    config.json \
    greenhouse-proxy.service \
    install.sh \
    copy-frontend.sh

# Also include ESP32 data files if they exist
if [ -d "../data" ]; then
    echo "  Including ESP32 frontend files..."
    mkdir -p /tmp/greenhouse-proxy-staging/public
    cp ../data/*.html /tmp/greenhouse-proxy-staging/public/ 2>/dev/null || true
    cp ../data/*.html.gz /tmp/greenhouse-proxy-staging/public/ 2>/dev/null || true
    
    # Decompress for Pi serving
    for gz in /tmp/greenhouse-proxy-staging/public/*.gz; do
        [ -f "$gz" ] && gunzip -kf "$gz" 2>/dev/null || true
    done
    
    # Add to tarball
    tar -rf /tmp/greenhouse-proxy.tar -C /tmp/greenhouse-proxy-staging public/
    gzip -f /tmp/greenhouse-proxy.tar
fi

# Copy to Pi
echo "[2/4] Copying to $PI_HOST..."
ssh "$PI_HOST" "mkdir -p $REMOTE_TEMP"
scp /tmp/greenhouse-proxy.tar.gz "$PI_HOST:$REMOTE_TEMP/"

# Extract and install on Pi
echo "[3/4] Installing on Pi..."
ssh "$PI_HOST" "cd $REMOTE_TEMP && tar -xzf greenhouse-proxy.tar.gz"
ssh -t "$PI_HOST" "cd $REMOTE_TEMP && sudo ./install.sh $INSTALL_ARGS"

# Cleanup
echo "[4/4] Cleaning up..."
ssh "$PI_HOST" "rm -rf $REMOTE_TEMP"
rm -f /tmp/greenhouse-proxy.tar.gz
rm -rf /tmp/greenhouse-proxy-staging

echo ""
echo "✅ Deployment complete!"
echo ""
echo "Access dashboard at: http://$PI_HOST:3000/"
echo "Or if Nginx enabled: http://$PI_HOST/"
