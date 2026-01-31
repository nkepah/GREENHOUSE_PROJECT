#!/bin/bash

# OTA Test Script - Push firmware update via OTA
# Usage: ./test_ota.sh [device_ip] [pi_ip]

DEVICE_IP="${1:?Error: DEVICE_IP required as first argument}"
PI_IP="${2:?Error: PI_IP required as second argument}"
FIRMWARE_FILE=".pio/build/esp32dev/firmware.bin"

echo "================================"
echo "🚀 OTA Firmware Update Test"
echo "================================"
echo ""
echo "Device IP: $DEVICE_IP"
echo "Pi IP: $PI_IP"
echo "Firmware: $FIRMWARE_FILE"
echo ""

# Check if firmware file exists
if [ ! -f "$FIRMWARE_FILE" ]; then
    echo "❌ Firmware file not found: $FIRMWARE_FILE"
    echo "Please compile first: platformio run -e esp32dev"
    exit 1
fi

FIRMWARE_SIZE=$(stat -f%z "$FIRMWARE_FILE" 2>/dev/null || stat -c%s "$FIRMWARE_FILE" 2>/dev/null)
echo "Firmware size: $FIRMWARE_SIZE bytes"
echo ""

# Step 1: Copy firmware to Pi
echo "Step 1: Copying firmware to Pi..."
ssh pi@$PI_IP "mkdir -p /opt/greenhouse-proxy/firmware" 2>/dev/null || true
scp "$FIRMWARE_FILE" "pi@$PI_IP:/opt/greenhouse-proxy/firmware/greenhouse.bin" || {
    echo "❌ Failed to copy firmware to Pi"
    exit 1
}
echo "✓ Firmware copied to Pi"
echo ""

# Step 2: Verify firmware on Pi
echo "Step 2: Verifying firmware on Pi..."
PI_FIRMWARE_SIZE=$(ssh pi@$PI_IP "stat -c%s /opt/greenhouse-proxy/firmware/greenhouse.bin 2>/dev/null || stat -f%z /opt/greenhouse-proxy/firmware/greenhouse.bin 2>/dev/null" 2>/dev/null)
if [ "$PI_FIRMWARE_SIZE" = "$FIRMWARE_SIZE" ]; then
    echo "✓ Firmware verified on Pi ($PI_FIRMWARE_SIZE bytes)"
else
    echo "⚠️  Warning: Firmware size mismatch!"
    echo "   Local: $FIRMWARE_SIZE bytes"
    echo "   Pi: $PI_FIRMWARE_SIZE bytes"
fi
echo ""

# Step 3: Check device connectivity
echo "Step 3: Checking device connectivity..."
ping -c 1 -W 1 $DEVICE_IP >/dev/null 2>&1 && {
    echo "✓ Device is reachable at $DEVICE_IP"
} || {
    echo "⚠️  Device at $DEVICE_IP may be offline"
}
echo ""

# Step 4: Instructions
echo "Step 4: Next steps..."
echo ""
echo "📋 The firmware is ready for OTA update."
echo ""
echo "Option A - Automatic (wait up to 1 hour):"
echo "  Device will automatically check Pi for updates"
echo "  Monitor with: platformio device monitor -p COM3"
echo ""
echo "Option B - Force Check (modify timeout for testing):"
echo "  Edit src/main.cpp around line 377:"
echo "  Change: if (millis() - lastOTACheck > 3600000)"
echo "  To:     if (millis() - lastOTACheck > 60000)  // 1 minute"
echo ""
echo "  Then recompile and upload:"
echo "  platformio run -e esp32dev --target=upload --upload-port=COM3"
echo ""
echo "Option C - Via API Call (if device allows):"
echo "  curl -X POST http://$DEVICE_IP/api/update"
echo ""
echo "📊 Monitor OTA Progress:"
echo "  platformio device monitor -p COM3 -b 115200 | grep -E 'OTA|Network'"
echo ""
echo "🔍 Check Device Status:"
echo "  curl http://$PI_IP:3000/api/device/*/info | jq"
echo ""
echo "================================"
echo "✅ OTA firmware ready to deploy!"
echo "================================"
