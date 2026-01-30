#!/bin/bash
#
# 🛑 Uninstall Greenhouse Proxy
#
# Completely removes the greenhouse proxy installation.
#
# Usage:
#   sudo ./uninstall.sh
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Please run as root (sudo ./uninstall.sh)${NC}"
    exit 1
fi

echo -e "${YELLOW}⚠️  This will remove the Greenhouse Proxy installation${NC}"
echo ""
read -p "Are you sure? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled"
    exit 1
fi

# Stop and disable service
echo "Stopping service..."
systemctl stop greenhouse-proxy 2>/dev/null || true
systemctl disable greenhouse-proxy 2>/dev/null || true
rm -f /etc/systemd/system/greenhouse-proxy.service
systemctl daemon-reload

# Remove installation directory
echo "Removing files..."
rm -rf /opt/greenhouse-proxy

# Remove Nginx config if exists
if [ -f /etc/nginx/sites-available/greenhouse ]; then
    echo "Removing Nginx configuration..."
    rm -f /etc/nginx/sites-available/greenhouse
    rm -f /etc/nginx/sites-enabled/greenhouse
    systemctl restart nginx 2>/dev/null || true
fi

echo ""
echo -e "${GREEN}✓ Greenhouse Proxy uninstalled${NC}"
echo ""
echo "Note: Node.js and Nginx were not removed."
echo "To remove them: sudo apt remove nodejs nginx"
