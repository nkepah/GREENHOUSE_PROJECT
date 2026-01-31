#!/bin/bash
# Deploy Greenhouse UI Files to Pi Server
# Usage: ./deploy_to_pi.sh [pi_hostname] [pi_user]

PI_HOST="${1:-farm-hub}"
PI_USER="${2:-pi}"
PI_WEBROOT="/var/www/greenhouse"

echo "============================================"
echo "Greenhouse UI Deployment to Pi"
echo "============================================"
echo "Target: ${PI_USER}@${PI_HOST}"
echo "Webroot: ${PI_WEBROOT}"
echo ""

# Check if files exist locally
echo "Checking local UI files..."
if [ ! -f "data/index.html" ]; then
    echo "❌ ERROR: data/index.html not found"
    exit 1
fi

if [ ! -f "data/alerts.html" ]; then
    echo "⚠️  WARNING: data/alerts.html not found (expected, may be in backup)"
fi

if [ ! -f "data/routines.html" ]; then
    echo "⚠️  WARNING: data/routines.html not found (expected, may be in backup)"
fi

echo "✅ Local files verified"
echo ""

# Create webroot on Pi if it doesn't exist
echo "Creating webroot on Pi..."
ssh "${PI_USER}@${PI_HOST}" "mkdir -p ${PI_WEBROOT}" || {
    echo "❌ ERROR: Could not create webroot on Pi"
    exit 1
}
echo "✅ Webroot created/verified"
echo ""

# Deploy index.html
echo "Deploying index.html..."
scp data/index.html "${PI_USER}@${PI_HOST}:${PI_WEBROOT}/" || {
    echo "❌ ERROR: Failed to deploy index.html"
    exit 1
}
echo "✅ index.html deployed"

# Deploy alerts.html (if available)
if [ -f "data/alerts.html" ]; then
    echo "Deploying alerts.html..."
    scp data/alerts.html "${PI_USER}@${PI_HOST}:${PI_WEBROOT}/" || {
        echo "❌ ERROR: Failed to deploy alerts.html"
    }
    echo "✅ alerts.html deployed"
fi

# Deploy routines.html (if available)
if [ -f "data/routines.html" ]; then
    echo "Deploying routines.html..."
    scp data/routines.html "${PI_USER}@${PI_HOST}:${PI_WEBROOT}/" || {
        echo "❌ ERROR: Failed to deploy routines.html"
    }
    echo "✅ routines.html deployed"
fi

echo ""
echo "Verifying deployment..."
ssh "${PI_USER}@${PI_HOST}" "ls -lh ${PI_WEBROOT}/"

echo ""
echo "============================================"
echo "Testing web access..."
echo "============================================"
echo ""
echo "To test, visit:"
echo "  • http://${PI_HOST}/"
echo "  • http://${PI_HOST}/index.html"
echo "  • http://${PI_HOST}/alerts.html (if deployed)"
echo "  • http://${PI_HOST}/routines.html (if deployed)"
echo ""
echo "Or test from command line:"
echo "  curl http://${PI_HOST}/index.html"
echo "  curl http://${PI_HOST}/alerts.html"
echo "  curl http://${PI_HOST}/routines.html"
echo ""
