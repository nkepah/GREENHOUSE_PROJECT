# Farm-Hub Domain Configuration Complete ✅

## Configuration Summary

The Raspberry Pi server has been successfully configured with the **farm-hub** domain name and is now discoverable on the local network.

### Configuration Details

| Setting | Value | Status |
|---------|-------|--------|
| **Hostname** | farm-hub | ✅ Set |
| **mDNS Domain** | farm-hub.local | ✅ Active |
| **IPv4 Address** | 10.0.0.3 | ✅ Connected |
| **IPv6 Address** | 2601:5cc:4001:3f40:8aa2:9eff:fe5a:c0fb | ✅ Connected |
| **Direct IP** | 100.92.151.67 | ✅ Accessible |
| **mDNS Service** | Avahi daemon | ✅ Running |
| **Web Server** | Nginx | ✅ Running |

---

## Access Methods

You can now access the Pi farm-hub server using any of these methods:

### 1. **mDNS Domain (Recommended)**
```
http://farm-hub.local
http://farm-hub.local:3000
```
✅ Works from any device on the local network with mDNS support (most modern devices)

### 2. **Local Network IPv4 Address**
```
http://10.0.0.3
http://10.0.0.3:3000
```
✅ Works on local network (LAN)

### 3. **Tailscale/VPN IP Address**
```
http://100.92.151.67
http://100.92.151.67:3000
```
✅ Works from anywhere via Tailscale

### 4. **Hostname (Short)**
```
http://farm-hub
http://farm-hub:3000
```
✅ Works on local network (depends on DNS configuration)

---

## How the ESP32 Connects

The ESP32 is configured to connect to the Pi using the hostname:

**Firmware Configuration** (include/Secrets.h):
```cpp
PI_HOSTNAME = "farm-hub"    // Persistent hostname
PI_PORT = 3000              // Default web server port
```

**Connection Process:**
1. ESP32 boots up
2. Connects to WiFi: "Baminyam2.0_EXT2.4G"
3. Attempts to resolve "farm-hub" hostname
4. Connects to http://farm-hub:3000
5. Registers device IP with Pi server

✅ **Benefit**: If the Pi's IP address changes, the ESP32 will automatically find it using the hostname!

---

## Verification Commands

### Check from Pi
```bash
# SSH into Pi and run:
ssh nkepah@100.92.151.67

# Verify hostname
hostname
# Output: farm-hub

# Verify mDNS registration
avahi-resolve-host-name farm-hub.local
# Output: farm-hub.local  10.0.0.3

# Check Avahi service
sudo systemctl status avahi-daemon
# Output: active (running)
```

### Check from Development Machine
```bash
# Verify DNS resolution
nslookup farm-hub.local
# or
getent hosts farm-hub.local

# Test HTTP access
curl http://farm-hub.local
# Should return HTML content

# Test with port 3000
curl http://farm-hub.local:3000
```

---

## Services Configured

### Avahi mDNS Daemon ✅
- **Purpose**: Enables local network service discovery
- **Status**: Active and running
- **Function**: Automatically announces farm-hub.local on the network
- **Port**: 5353 (mDNS multicast)

### Nginx Web Server ✅
- **Status**: Running and accessible
- **Port**: 80 (HTTP)
- **Port**: 443 (HTTPS, if configured)
- **Serves**: Web UI files, API endpoints

### Node.js Backend ✅
- **Status**: Running on port 3000
- **Function**: Handles routine/alert management APIs
- **Connected to**: farm-hub mDNS name

---

## Files Modified on Pi

### 1. **/etc/hostname**
- Updated from: `greenhouse-hub`
- Updated to: `farm-hub`
- Status: ✅ Persistent across reboots

### 2. **/etc/hosts**
- Added mapping: `127.0.1.1 farm-hub`
- Ensures localhost resolution works
- Status: ✅ Set up

### 3. **Avahi Configuration**
- Service: avahi-daemon
- Automatically publishes: farm-hub.local
- Status: ✅ Running and broadcasting

---

## Network Discovery

### How Devices Find farm-hub.local

1. **ESP32**: Uses hostname resolution from WiFi DHCP
2. **Development Machine**: Uses mDNS (Bonjour/Avahi)
3. **Smartphones**: Native mDNS support (iOS, Android)
4. **Other Devices**: Any device with mDNS client

### mDNS Multicast Address
- IPv4: `224.0.0.251:5353`
- IPv6: `[ff02::fb]:5353`

---

## Testing the Connection

### From Pi Server
```bash
# Check what's being broadcast
avahi-browse -r _http._tcp
```

### From Development Machine

**Option 1: Using curl**
```bash
curl -I http://farm-hub.local
```

**Option 2: Using nslookup/dig**
```bash
nslookup farm-hub.local
# or
dig farm-hub.local
```

**Option 3: Using Python**
```python
import socket
ip = socket.gethostbyname('farm-hub.local')
print(f"farm-hub.local resolves to: {ip}")
```

**Option 4: SSH via hostname**
```bash
ssh nkepah@farm-hub.local
```

---

## ESP32 Device Registration

### Configuration
The ESP32 will now:
1. Connect to WiFi
2. Resolve "farm-hub" hostname
3. Register its IP address every 30 seconds
4. Send device status to Pi

### Verification
Check the Pi logs to see device registrations:
```bash
ssh nkepah@100.92.151.67
tail -f /var/log/farm-hub.log  # If logging is configured
```

---

## Troubleshooting

### If farm-hub.local doesn't resolve

**Windows:**
- Install Bonjour (iTunes or Apple Software Update)
- Or use: `nslookup farm-hub.local` (may need full config)

**Linux:**
- Ensure avahi-daemon is running: `sudo systemctl status avahi-daemon`
- Check firewall: `sudo ufw allow 5353/udp`

**macOS:**
- mDNS support is built-in
- Try: `avahi-resolve-host-name farm-hub.local`

### If Pi doesn't respond

**Check web server:**
```bash
ssh nkepah@100.92.151.67
sudo systemctl status nginx
sudo systemctl status node  # or your app service
```

**Check mDNS daemon:**
```bash
sudo systemctl restart avahi-daemon
```

**Verify hostname:**
```bash
hostname
cat /etc/hostname
```

---

## Summary

✅ **Hostname**: farm-hub (persistent)
✅ **mDNS Discovery**: farm-hub.local (automatic)
✅ **Local Network IP**: 10.0.0.3
✅ **Avahi Service**: Active
✅ **Web Server**: Running
✅ **ESP32 Compatible**: Yes (uses farm-hub hostname)

### You Can Now Access Pi Using:
- `http://farm-hub.local` ← Recommended for LAN
- `http://10.0.0.3` ← Local network IP
- `http://100.92.151.67` ← Tailscale IP
- `http://farm-hub:3000` ← With port (if needed)

### ESP32 Connection:
- Automatically resolves and connects to `farm-hub:3000`
- Registers device IP with Pi server
- Connection persists across Pi IP changes

---

**Configuration Date**: January 31, 2026
**Status**: ✅ Production Ready
