#!/usr/bin/env python3
"""
OTA Upload Script for ESP32 Greenhouse Controller
Usage: python ota_upload.py <ESP32_IP> [--firmware] [--filesystem]
"""

import sys
import requests
import os
from pathlib import Path

def upload_ota(ip_address, firmware_path=None, filesystem_path=None):
    """Upload firmware and/or filesystem via OTA"""
    
    base_url = f"http://{ip_address}"
    
    # Upload firmware
    if firmware_path:
        if not os.path.exists(firmware_path):
            print(f"❌ Firmware file not found: {firmware_path}")
            return False
            
        print(f"📤 Uploading firmware to {base_url}/ota...")
        print(f"   File: {firmware_path} ({os.path.getsize(firmware_path)} bytes)")
        
        try:
            with open(firmware_path, 'rb') as f:
                files = {'update': ('firmware.bin', f, 'application/octet-stream')}
                response = requests.post(f"{base_url}/ota", files=files, timeout=120)
                
            if response.status_code == 200:
                result = response.json()
                if result.get('status') == 'success':
                    print("✅ Firmware uploaded successfully! Device is rebooting...")
                    # Wait for reboot
                    import time
                    time.sleep(10)
                else:
                    print(f"❌ Upload failed: {result.get('message', 'Unknown error')}")
                    return False
            else:
                print(f"❌ Upload failed with status {response.status_code}")
                print(f"   Response: {response.text[:200]}")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f"❌ Connection error: {e}")
            return False
    
    # Upload filesystem
    if filesystem_path:
        if not os.path.exists(filesystem_path):
            print(f"❌ Filesystem file not found: {filesystem_path}")
            return False
            
        print(f"📤 Uploading filesystem to {base_url}/ota...")
        print(f"   File: {filesystem_path} ({os.path.getsize(filesystem_path)} bytes)")
        
        try:
            with open(filesystem_path, 'rb') as f:
                files = {'update': ('littlefs.bin', f, 'application/octet-stream')}
                # Send type parameter to identify filesystem upload
                data = {'type': 'filesystem'}
                response = requests.post(f"{base_url}/ota", files=files, data=data, timeout=180)
                
            if response.status_code == 200:
                result = response.json()
                if result.get('status') == 'success':
                    print("✅ Filesystem uploaded successfully! Device is rebooting...")
                else:
                    print(f"❌ Upload failed: {result.get('message', 'Unknown error')}")
                    return False
            else:
                print(f"❌ Upload failed with status {response.status_code}")
                print(f"   Response: {response.text[:200]}")
                return False
                
        except requests.exceptions.RequestException as e:
            print(f"❌ Connection error: {e}")
            return False
    
    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: python ota_upload.py <ESP32_IP> [--firmware] [--filesystem] [--both]")
        print("\nExamples:")
        print("  python ota_upload.py 10.0.0.163 --firmware")
        print("  python ota_upload.py 10.0.0.163 --filesystem")
        print("  python ota_upload.py 10.0.0.163 --both")
        sys.exit(1)
    
    ip_address = sys.argv[1]
    
    # Default paths
    project_dir = Path(__file__).parent
    firmware_path = project_dir / ".pio" / "build" / "esp32dev" / "firmware.bin"
    filesystem_path = project_dir / ".pio" / "build" / "esp32dev" / "littlefs.bin"
    
    upload_firmware = "--firmware" in sys.argv or "--both" in sys.argv
    upload_filesystem = "--filesystem" in sys.argv or "--both" in sys.argv
    
    # If no flags specified, default to firmware only
    if not upload_firmware and not upload_filesystem:
        upload_firmware = True
    
    print(f"🌱 Greenhouse OTA Uploader")
    print(f"   Target: {ip_address}")
    print(f"   Firmware: {'Yes' if upload_firmware else 'No'}")
    print(f"   Filesystem: {'Yes' if upload_filesystem else 'No'}")
    print()
    
    success = upload_ota(
        ip_address,
        firmware_path if upload_firmware else None,
        filesystem_path if upload_filesystem else None
    )
    
    if success:
        print("\n✅ OTA update completed successfully!")
        sys.exit(0)
    else:
        print("\n❌ OTA update failed!")
        sys.exit(1)

if __name__ == "__main__":
    main()
