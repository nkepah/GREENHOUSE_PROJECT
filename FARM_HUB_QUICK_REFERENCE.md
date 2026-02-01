# Farm-Hub Quick Reference

## Access Points

| Method | Address | Network | Latency |
|--------|---------|---------|---------|
| **mDNS** (Recommended) | `farm-hub.local` | Local LAN | Low |
| **IPv4** | `10.0.0.3` | Local LAN | Very Low |
| **IPv4 + Port** | `10.0.0.3:3000` | Local LAN | Very Low |
| **Tailscale** | `100.92.151.67` | VPN | Medium |
| **Hostname** | `farm-hub` | Local LAN | Low |
| **Hostname + Port** | `farm-hub:3000` | Local LAN | Low |

## Common Commands

### Test Connection
```bash
curl http://farm-hub.local
```

### Check DNS Resolution
```bash
nslookup farm-hub.local
```

### SSH to Pi
```bash
ssh nkepah@farm-hub.local
ssh nkepah@100.92.151.67
```

### View Pi Configuration
```bash
ssh nkepah@100.92.151.67 "hostname && avahi-resolve-host-name farm-hub.local"
```

### Restart Avahi Service (if needed)
```bash
ssh nkepah@100.92.151.67 "sudo systemctl restart avahi-daemon"
```

## ESP32 Configuration

### Current Settings (include/Secrets.h)
```cpp
PI_HOSTNAME = "farm-hub"    // Resolves to 10.0.0.3
PI_PORT = 3000
```

### What ESP32 Does
1. Boots and connects to WiFi
2. Resolves `farm-hub` → `10.0.0.3`
3. Connects to `http://farm-hub:3000`
4. Registers device IP with Pi every 30 seconds

### How to Verify ESP32 Connection
```bash
# From Pi, check for device registration logs
ssh nkepah@100.92.151.67 "journalctl -u farm-hub -f"  # If systemd service
# or
ssh nkepah@100.92.151.67 "tail -f /var/log/farm-hub.log"
```

## Web Server Access

### Main Dashboard
```
http://farm-hub.local
or
http://farm-hub.local:3000
```

### API Endpoints
```
GET  http://farm-hub.local/api/devices
GET  http://farm-hub.local/api/status
POST http://farm-hub.local/api/routine/execute
```

### UI Pages
```
http://farm-hub.local/index.html
http://farm-hub.local/alerts.html
http://farm-hub.local/routines.html
```

## Troubleshooting

### farm-hub.local not resolving?
```bash
# On Pi, verify mDNS is running
ssh nkepah@100.92.151.67 "sudo systemctl status avahi-daemon"

# Restart if needed
ssh nkepah@100.92.151.67 "sudo systemctl restart avahi-daemon"
```

### Connection to Pi fails?
```bash
# Check if Pi is online
ping -c 1 100.92.151.67  # Direct IP

# Or try Tailscale
ping -c 1 farm-hub  # If on same Tailscale network
```

### ESP32 can't connect to farm-hub?
```bash
# Check ESP32 serial output for DNS resolution errors
# WiFi must be working first
# Then device should show: "Registered device IP with Pi"
```

## Network Details

**Pi Local Network**:
- Hostname: `farm-hub`
- mDNS: `farm-hub.local`
- IPv4: `10.0.0.3`
- IPv6: `2601:5cc:4001:3f40:8aa2:9eff:fe5a:c0fb`

**Pi Tailscale**:
- IP: `100.92.151.67`
- SSH: `nkepah@100.92.151.67`

**mDNS Multicast**:
- IPv4: `224.0.0.251:5353`
- IPv6: `[ff02::fb]:5353`

## Status

✅ Hostname: farm-hub
✅ mDNS: Active (Avahi)
✅ Web Server: Running (Nginx)
✅ Backend: Running (Node.js:3000)
✅ ESP32: Compatible

---

**Last Updated**: January 31, 2026
**Configuration**: Production Ready
