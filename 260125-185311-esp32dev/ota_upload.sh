#!/bin/bash
# Quick OTA Upload Script using curl  
# Usage: ./ota_upload.sh <IP_ADDRESS> [--firmware|--filesystem|--both]

IP=${1:-10.0.0.163}
FIRMWARE=".pio/build/esp32dev/firmware.bin"
FILESYSTEM=".pio/build/esp32dev/littlefs.bin"

echo "🌱 Greenhouse OTA Upload"
echo "   Target: $IP"
echo ""

upload_file() {
    local file=$1
    local type=$2
    local extra_args=$3
    
    echo "📤 Uploading $type..."
    echo "   File: $file ($(du -h $file | cut -f1))"
    
    if [ -n "$extra_args" ]; then
        curl --progress-bar -F "update=@$file" $extra_args http://$IP/ota
    else
        curl --progress-bar -F "update=@$file" http://$IP/ota
    fi
    
    # Connection reset is expected - device reboots after OTA
    if [ $? -eq 56 ] || [ $? -eq 0 ]; then
        echo ""
        echo "✅ $type uploaded! Device is rebooting..."
        echo "   Waiting 10 seconds for reboot..."
        sleep 10
        return 0
    else
        echo ""
        echo "❌ Upload failed!"
        return 1
    fi
}

if [ "$2" == "--both" ]; then
    upload_file "$FIRMWARE" "Firmware" ""
    if [ $? -eq 0 ]; then
        upload_file "$FILESYSTEM" "Filesystem" '-F "type=filesystem"'
    fi
elif [ "$2" == "--fs" ] || [ "$2" == "--filesystem" ]; then
    upload_file "$FILESYSTEM" "Filesystem" '-F "type=filesystem"'
else
    upload_file "$FIRMWARE" "Firmware" ""
fi

echo ""
echo "🎉 OTA update complete!"
