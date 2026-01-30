#!/bin/bash
#
# 🔍 Greenhouse Proxy Status Check
#
# Quick status overview of the proxy server and ESP32 connection.
#
# Usage:
#   ./status.sh                    # Check default (100.92.151.67)
#   ./status.sh 100.92.151.67      # Check specific Pi
#

PI_HOST="${1:-100.92.151.67}"
PORT="${2:-3000}"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo ""
echo -e "${BLUE}🌿 Greenhouse Proxy Status${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Check if proxy is reachable
STATUS=$(curl -s "http://$PI_HOST:$PORT/api/status" 2>/dev/null)

if [ -z "$STATUS" ]; then
    echo -e "${RED}✗ Proxy server not reachable at $PI_HOST:$PORT${NC}"
    echo ""
    echo "Possible issues:"
    echo "  - Server not running: sudo systemctl start greenhouse-proxy"
    echo "  - Wrong IP address"
    echo "  - Firewall blocking port $PORT"
    exit 1
fi

# Parse status JSON
VERSION=$(echo "$STATUS" | grep -o '"version":"[^"]*"' | cut -d'"' -f4)
UPTIME=$(echo "$STATUS" | grep -o '"uptime":[0-9.]*' | cut -d':' -f2)
ESP32_CONNECTED=$(echo "$STATUS" | grep -o '"connected":[^,}]*' | head -1 | cut -d':' -f2)
ESP32_IP=$(echo "$STATUS" | grep -o '"ip":"[^"]*"' | head -1 | cut -d'"' -f4)
BROWSERS=$(echo "$STATUS" | grep -o '"connected":[0-9]*' | tail -1 | cut -d':' -f2)
WEATHER_AGE=$(echo "$STATUS" | grep -o '"age":"[^"]*"' | cut -d'"' -f4)

# Format uptime
if [ -n "$UPTIME" ]; then
    UPTIME_MINS=$(echo "$UPTIME / 60" | bc 2>/dev/null || echo "?")
    UPTIME_STR="${UPTIME_MINS}m"
else
    UPTIME_STR="?"
fi

echo -e "Server:     ${GREEN}Running${NC} (v$VERSION, uptime: $UPTIME_STR)"
echo ""

# ESP32 status
if [ "$ESP32_CONNECTED" == "true" ]; then
    echo -e "ESP32:      ${GREEN}✓ Connected${NC} ($ESP32_IP)"
else
    echo -e "ESP32:      ${RED}✗ Disconnected${NC} (target: $ESP32_IP)"
fi

# Browser clients
echo -e "Browsers:   ${BLUE}$BROWSERS connected${NC}"

# Weather cache
if [ -n "$WEATHER_AGE" ] && [ "$WEATHER_AGE" != "null" ]; then
    echo -e "Weather:    Cached ($WEATHER_AGE ago)"
else
    echo -e "Weather:    ${YELLOW}No cache${NC}"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Quick links
echo "Dashboard:  http://$PI_HOST:$PORT/"
echo "API:        http://$PI_HOST:$PORT/api/status"
echo ""

# Service status (if checking localhost)
if [ "$PI_HOST" == "localhost" ] || [ "$PI_HOST" == "127.0.0.1" ]; then
    echo "Service:    $(systemctl is-active greenhouse-proxy 2>/dev/null || echo 'unknown')"
    echo ""
fi
