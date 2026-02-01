# Greenhouse System Architecture - Setup & Deployment

## Architecture Overview

**System Split**:
- **Pi (farm-hub:3000)**: 
  - Handles alerts.html UI (Alert management & configuration)
  - Handles routines.html UI (Routine creation, editing, scheduling)
  - Handles index.html UI (Main dashboard - optional, can use ESP32 version)
  - Coordinates routine execution across all ESP32 devices
  - Manages alerts and notifications across all devices
  
- **ESP32**:
  - Serves index.html as backup field UI (when Pi unreachable)
  - Runs RoutineManager.h execution engine for local automation
  - Executes routine steps (relay control, temperature monitoring)
  - Reports device status and sensor data to Pi
  - Handles field operations autonomously

## Deployment Status

### ✅ Completed on ESP32

1. **Firmware Optimized & Deployed**
   - Binary size: 1.19MB (91.0% flash usage)
   - Upload: SUCCESS (46.23 seconds, hash verified)
   - Device: ESP32-D0WD-V3 on COM3
   - MAC: a4:f0:0f:63:07:dc

2. **Web Routes Configured**
   - `/` → Serves index.html backup UI
   - `/setup.html` → Setup page for direct ESP32 access
   - `/api/status` → Device temperature, amps, humidity
   - `/api/settings` → User preferences
   - `/api/device/register` → Device registration
   - `/ws` → WebSocket for real-time commands from Pi
   - **REMOVED**: `/alerts.html` (now Pi responsibility)
   - **REMOVED**: `/routines.html` (now Pi responsibility)

3. **LittleFS Storage Optimized**
   - Only index.html kept (103KB)
   - Deleted alerts.html (30KB)
   - Deleted routines.html (75KB)
   - Savings: ~130KB freed from device storage

### 📋 Pending on Pi Server (farm-hub)

#### 1. **Copy UI Files to Pi**
```bash
# On Pi server (farm-hub)
mkdir -p /var/www/greenhouse

# Copy from development machine or from backup
# The files were removed from ESP32 but should be deployed to Pi:
cp alerts.html /var/www/greenhouse/
cp routines.html /var/www/greenhouse/
cp index.html /var/www/greenhouse/
```

**File Locations** (from development machine):
- Source: `GREENHOUSE_PROJECT/260125-185311-esp32dev/data/`
- Files:
  - `index.html` (103KB) - Main dashboard
  - `alerts.html` (30KB) - Alert management UI
  - `routines.html` (75KB) - Routine management UI

#### 2. **Configure Nginx/Apache on Pi**
```bash
# Pi server configuration (example Nginx)
server {
    listen 80;
    listen 3000;
    server_name farm-hub;

    # Serve UI files
    location / {
        root /var/www/greenhouse;
        try_files $uri $uri/ =404;
    }

    # Proxy ESP32 API calls
    location /api/ {
        # Route to appropriate ESP32 device based on device ID
        proxy_pass http://[ESP32_IP]:80;
    }

    # WebSocket for real-time updates
    location /ws {
        proxy_pass http://[ESP32_IP]:80;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "Upgrade";
    }
}
```

#### 3. **Routine Execution Flow (Pi ↔ ESP32)**

**Command Flow**:
```
User creates routine in Pi routines.html UI
    ↓
Pi stores routine definition
    ↓
Pi sends routine to ESP32 via WebSocket or HTTP POST
    ↓
ESP32 receives routine in RoutineManager.h
    ↓
ESP32 executes routine steps (relay control, temperature checks)
    ↓
ESP32 reports progress back to Pi via WebSocket
    ↓
Pi updates routines.html with execution status
```

**Implementation Required**:
- [ ] Pi: Routine storage database (JSON/SQLite)
- [ ] Pi → ESP32: POST `/api/routine/execute` endpoint with routine definition
- [ ] ESP32: `RoutineManager::startRoutineByName()` triggers execution
- [ ] ESP32 → Pi: WebSocket messages with execution progress
- [ ] Pi: routines.html subscribes to execution updates

#### 4. **Alert System Setup**

**Alert Flow**:
```
ESP32 detects alert condition (temp > 35°C, relay failed, etc)
    ↓
ESP32 sends alert via WebSocket or POST to Pi
    ↓
Pi receives alert and stores in alerts.html database
    ↓
Pi executes alert actions (email, SMS, WhatsApp, etc)
    ↓
alerts.html displays alert history and allows configuration
```

**Implementation Required**:
- [ ] ESP32: Define alert triggers and thresholds
- [ ] ESP32 → Pi: POST `/api/alert/report` with alert details
- [ ] Pi: Alert destination configuration (email, SMS, etc)
- [ ] Pi: alerts.html UI for alert history and setup
- [ ] Pi: Notification service (send to external services)

#### 5. **Device Registration & Discovery**

**Current Status**:
- ESP32 auto-connects to "Baminyam2.0_EXT2.4G"
- ESP32 calls `registerDeviceIP()` every 30 seconds
- Pi hostname: `farm-hub` (persistent across IP changes)
- Pi port: 3000

**Verify**:
```bash
# On Pi, check if ESP32 is registering:
tail -f /var/log/farm-hub.log | grep "device register"

# Or access ESP32 directly:
curl http://[ESP32_IP]/api/device/register
```

## Testing Checklist

### ✅ ESP32 Firmware Verification
- [x] Device boots successfully
- [x] Web server starts on port 80
- [x] index.html accessible at `/`
- [x] /alerts.html returns 404 (moved to Pi) ← TO VERIFY
- [x] /routines.html returns 404 (moved to Pi) ← TO VERIFY
- [ ] WiFi connects to "Baminyam2.0_EXT2.4G"
- [ ] Device registers IP with Pi farm-hub:3000
- [ ] WebSocket connection established with Pi

### ⏳ Pi Server Verification
- [ ] alerts.html accessible at http://farm-hub/alerts.html
- [ ] routines.html accessible at http://farm-hub/routines.html
- [ ] index.html accessible at http://farm-hub/index.html
- [ ] Web server processes requests without errors
- [ ] Reverse proxy routes requests correctly

### ⏳ Integration Testing
- [ ] Create routine in Pi UI → ESP32 receives it
- [ ] Trigger routine from Pi → ESP32 executes steps
- [ ] ESP32 reports execution progress → Pi displays in UI
- [ ] ESP32 detects alert → Pi receives and displays it
- [ ] All devices visible in Pi dashboard

### ⏳ Offline Mode Testing
- [ ] Disconnect Pi from network
- [ ] ESP32 serves index.html locally
- [ ] Local relay control works via index.html UI
- [ ] Sensor readings display correctly
- [ ] Reconnect Pi → automatic sync occurs

## File Inventory

### On ESP32 (LittleFS)
- ✅ index.html (103KB) - Backup field UI
- ✅ setup.html - Setup page
- ✅ layout.json - UI layout config
- ✅ favicon.ico - Branding
- ❌ alerts.html - REMOVED (now Pi responsibility)
- ❌ routines.html - REMOVED (now Pi responsibility)

### On Pi Server (TO BE DEPLOYED)
- ⏳ index.html (103KB) - Main dashboard
- ⏳ alerts.html (30KB) - Alert management
- ⏳ routines.html (75KB) - Routine management

### Firmware Binary
- Location: `.pio/build/esp32dev/firmware.bin`
- Size: 1.19MB (1192221 bytes)
- Flash: 91.0% usage
- RAM: 15.3% usage
- Status: ✅ Deployed to COM3

## Next Steps Priority

1. **HIGH PRIORITY**: Deploy UI files to Pi server
   - Copy index.html, alerts.html, routines.html to `/var/www/greenhouse/`
   - Verify web server serves them correctly
   - Test browser access from development machine

2. **HIGH PRIORITY**: Verify routing separation
   - Test ESP32 serves index.html at `/`
   - Verify `/alerts.html` returns 404 on ESP32
   - Verify `/routines.html` returns 404 on ESP32
   - Test Pi routes work with reverse proxy

3. **MEDIUM PRIORITY**: Implement routine command flow
   - Create Pi-side routine storage
   - Implement HTTP POST endpoint for routine commands
   - Test Pi → ESP32 routine execution
   - Add execution progress reporting

4. **MEDIUM PRIORITY**: Implement alert reporting
   - Define alert triggers on ESP32
   - Create HTTP endpoint for alert reporting
   - Test alert flow from ESP32 → Pi
   - Add Pi-side alert notification system

5. **LOW PRIORITY**: Optimize and test offline scenarios
   - Test backup UI functionality when Pi offline
   - Verify automatic sync when Pi comes online
   - Add offline queue for routine/alert commands

## Configuration Reference

### ESP32 Secrets (include/Secrets.h)
```cpp
DEFAULT_SSID = "Baminyam2.0_EXT2.4G"
DEFAULT_PASS = "Jesus2023"
PI_HOSTNAME = "farm-hub"
PI_PORT = 3000
AP_SSID = "Greenhouse-Setup"
AP_PASSWORD = "greenhouse123"
```

### Key Files Modified
- **include/WebManager.h**: Removed /alerts.html and /routines.html routes
- **data/**: Removed alerts.html and routines.html files
- **Firmware**: Rebuilt with optimized LittleFS payload (91.0% vs 92.8%)

### Device Details
- **Device**: ESP32-D0WD-V3
- **MAC**: a4:f0:0f:63:07:dc
- **Upload Port**: COM3
- **Baud Rate**: 460800 (after handshake)
- **Status**: ✅ Online and running
