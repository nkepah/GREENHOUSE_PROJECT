# Hardcoded IPs Removal - Summary

**Date**: January 31, 2026  
**Status**: ✅ Complete

## What Was Changed

### 1. **Server Configuration (server.js)**
**Removed hardcoded IPs** from the initial device configuration:

```javascript
// BEFORE:
devices: {
    greenhouse: { ip: "192.168.1.100", name: "Greenhouse", type: "greenhouse" },
    coop1: { ip: "192.168.1.101", name: "Coop 1", type: "coop" },
    ...
}

// AFTER:
devices: {
    greenhouse: { name: "Greenhouse", type: "greenhouse" },
    coop1: { name: "Coop 1", type: "coop" },
    ...
}
```

### 2. **Device Registration Endpoints**
Updated to save and return **dynamic IPs** from device registrations:

- **`POST /api/device/register`** - First registration (HIGH priority, -20 nice)
- **`POST /api/device/update-ip`** - Every 30 minutes (LOW priority, +19 nice)

Both endpoints now update `config.devices[device_id].ip` with the actual device IP.

### 3. **API Endpoints**
All device retrieval endpoints now return **dynamically registered IPs**:

| Endpoint | Change |
|----------|--------|
| `GET /api/devices` | Returns `ip` from device registration |
| `GET /api/devices/:id` | Returns `ip` from device registration |
| `GET /api/dashboard` | Returns device list with registered IPs |
| `/device/:deviceId/*` | Uses dynamic IP for device proxy |

**Fallback Logic**: 
```javascript
const ip = info.ip || (deviceStatus[deviceId]?.ip);
```
Uses registered IP if available, falls back to config if needed.

### 4. **ESP32 Firmware**
Updated IP check interval from **30 seconds** to **30 minutes**:

```cpp
// BEFORE:
static constexpr unsigned long IP_CHECK_INTERVAL = 30000UL; // 30 seconds

// AFTER:
static constexpr unsigned long IP_CHECK_INTERVAL = 1800000UL; // 30 minutes
```

**Behavior**:
1. ESP32 connects to WiFi
2. Calls `registerDeviceWithPi()` - registers with HIGH priority
3. After 30 minutes, calls `registerDeviceIP()` - updates IP with LOW priority
4. Re-registers if IP changes or after 1 hour timeout

## How It Works

### Device Discovery Flow

```
1. ESP32 Boots
   ↓
2. Connects to WiFi (gets IP from DHCP)
   ↓
3. POSTs to http://smartfarmshub.local:3000/api/device/register
   {
     "device_id": "greenhouse",
     "hostname": "greenhouse",
     "ip_address": "10.0.0.163",  ← Device saves its own IP
     "mac_address": "...",
     "rssi": -45
   }
   ↓
4. Pi Server Updates config.devices[greenhouse].ip = "10.0.0.163"
   Sets priority to HIGH (-20)
   ↓
5. After 30 seconds: Pi drops priority to LOW (+19)
   ↓
6. Every 30 minutes: ESP32 calls /api/device/update-ip
   Re-registers IP at LOW priority
   ↓
7. Dashboard Requests GET /api/devices
   Pi Returns:
   {
     "greenhouse": {
       "name": "Greenhouse",
       "type": "greenhouse",
       "ip": "10.0.0.163",  ← From device registration!
       "online": true,
       "status": { ... }
     }
   }
   ↓
8. Dashboard UI Displays Tiles
   When user clicks "Greenhouse" tile:
   - Fetches from http://smartfarmshub.local:3000/device/greenhouse/
   - Pi proxies to http://10.0.0.163/ (the registered device IP)
   - User sees device UI
```

### Priority Management

| Event | Priority | Purpose |
|-------|----------|---------|
| ESP32 first registers | HIGH (-20) | Fast DB updates, immediate responses |
| 30 seconds later | Drops to LOW (+19) | Frees CPU for dashboard, web server |
| Every 30 minutes | LOW (+19) | IP update is non-critical, low overhead |

This ensures ESP32 registration doesn't block the web dashboard from responding to users.

## Files Modified

1. **`raspberry-pi/server.js`**
   - Removed hardcoded IPs from config
   - Updated `/api/devices` endpoints
   - Updated `/device/:deviceId/*` proxy
   - All endpoints now use dynamic IPs from registration

2. **`src/main.cpp`**
   - Changed `IP_CHECK_INTERVAL` from 30s to 30 minutes
   - No other changes (registration already existed)

3. **`include/Secrets.h`**
   - No changes needed (already uses `PI_DOMAIN` for mDNS)

4. **Dashboard (`index.html`)**
   - No changes needed (already reads from `/api/devices`)

## Testing

### Manual Test Sequence

1. **Check Devices Are Discovered**
   ```bash
   # Should return all devices with null IPs initially
   curl http://smartfarmshub.local:3000/api/devices
   
   # After ESP32 registers:
   # Should return device with registered IP
   ```

2. **Verify IP Registration**
   ```bash
   # Check server logs for device registration
   ssh nkepah@100.92.151.67 "tail -50 /tmp/server.log | grep DEVICE"
   ```

3. **Test Device Proxy**
   ```bash
   # Should proxy to the registered device IP
   curl http://smartfarmshub.local:3000/device/greenhouse/api/status
   ```

4. **Test Dashboard**
   - Open dashboard
   - Should show Greenhouse tile
   - Click tile → Should show device IP (now from registration)
   - Click "Open UI" → Should connect to device via registered IP

## Benefits

✅ **Dynamic Discovery** - No hardcoded IPs, devices self-register  
✅ **DHCP Friendly** - Works with any IP assignment  
✅ **IP Change Handling** - Automatically re-registers if IP changes  
✅ **Low CPU Overhead** - Updates happen at lowest priority  
✅ **High Priority First** - Initial registration doesn't block web server  
✅ **Scalable** - Works with any number of devices

## Edge Cases Handled

1. **Device not registered yet**: API returns `ip: null`, tile shows as "Offline"
2. **Device IP changed**: ESP32 detects change, re-registers automatically
3. **Device offline**: API shows `online: false` from last status check
4. **Multiple registration attempts**: Server updates IP each time, no duplicates
5. **Priority drops after 30s**: Only first registration uses HIGH priority

---

**Result**: Greenhouse tile should now connect using the dynamically registered device IP instead of hardcoded addresses. ✅
