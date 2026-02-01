# Routine Execution System Integration Guide

## Overview

The Greenhouse system now has a clear separation:
- **Pi (farm-hub)**: Routine UI management (routines.html)
- **ESP32**: Routine execution engine (RoutineManager.h)

This guide explains how to connect them.

---

## ESP32 Routine Execution Engine

### RoutineManager.h Structure

The ESP32 has a complete routine execution engine ready to use:

```cpp
class RoutineManager {
public:
    // Initialize the routine system
    void init();
    
    // Set the current threshold for amp monitoring
    void setAmpThreshold(float threshold);
    
    // Register callback when routine devices fail
    void setFailureCallback(std::function<void(const String&, const std::vector<DeviceConfirmResult>&)> cb);
    
    // Check if any routines should trigger based on conditions
    void checkTriggers(float temp, float weatherTemp, DeviceManager& devMgr, RelayController& relays,
                       int hour, int minute, int dayOfWeek, int dayOfMonth, int month);
    
    // Execute all active routines
    void processRoutines(DeviceManager& devMgr, RelayController& relays,
                        std::function<void(const String&, int, int, int)> progressCallback);
    
    // Trigger a specific routine by name
    void startRoutineByName(const String& routineName);
};

struct DeviceConfirmResult {
    String deviceId;              // Device being controlled
    String deviceName;            // Friendly device name
    int channel;                  // Relay channel number
    bool targetState;             // Desired state (on/off)
    float deltaAmps;              // Amp change detected
    bool confirmed;               // Whether change was confirmed
};
```

### Current Implementation Status

✅ **Available in main.cpp**:
- RoutineManager class instantiation
- Integration with sensor data and relay control
- WebSocket communication ready for commands from Pi

❌ **Not Yet Implemented**:
- Pi → ESP32 routine command reception
- Routine storage on ESP32
- Real-time progress reporting to Pi
- Alert triggering from routine failures

---

## Integration Steps

### Step 1: Add Routine Storage on ESP32

**Create a new file: `include/RoutineStorage.h`**

```cpp
#ifndef ROUTINE_STORAGE_H
#define ROUTINE_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>

struct RoutineDefinition {
    String id;
    String name;
    String description;
    bool enabled;
    // ... add routine properties as needed
};

class RoutineStorage {
public:
    void init();
    
    // Store routine from Pi
    void storeRoutine(const JsonDocument& routineJson);
    
    // Retrieve routine by ID
    RoutineDefinition getRoutine(const String& routineId);
    
    // Get all routines
    std::vector<RoutineDefinition> getAllRoutines();
    
    // Delete routine
    void deleteRoutine(const String& routineId);
    
private:
    Preferences prefs;
};

#endif
```

### Step 2: Add HTTP Endpoint for Routine Commands

**Add to `include/WebManager.h`**

```cpp
// Add this in the WebManager::begin() method:

// Receive routine command from Pi
server.on("/api/routine/execute", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Extract routine ID from request
    // Trigger RoutineManager::startRoutineByName()
    // Return confirmation
}, [](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Handle routine JSON data
    // Parse routine definition
    // Store in RoutineStorage
    // Start routine execution
});

// Store routine definition (for later execution)
server.on("/api/routine/define", HTTP_POST, [this](AsyncWebServerRequest *request) {
    // Receive routine definition from Pi
    // Store in RoutineStorage
    // Return success/failure
});

// Get routine status
server.on("/api/routine/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
    // Return current routine execution status
    // Include progress percentage
    // Include last error if any
});
```

### Step 3: Add WebSocket Routine Progress Reporting

**Modify WebManager.h WebSocket handler:**

```cpp
ws.onEvent([this](AsyncWebSocket * server, AsyncWebSocketClient * client, 
            AwsEventType type, void * arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) {
        // Send routine status on connect
        DynamicJsonDocument doc(512);
        doc["type"] = "routine_status";
        doc["executing"] = routine.isExecuting();
        doc["progress"] = routine.getProgress();
        
        String json;
        serializeJson(doc, json);
        server->textAll(json);
    }
    else if(type == WS_EVT_DATA) {
        // Handle incoming routine commands from Pi
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, (char*)data);
        
        if(doc["command"] == "execute_routine") {
            // Trigger routine execution
            routine.startRoutineByName(doc["routine_id"]);
        }
    }
});

// During routine execution, send progress updates:
routine.processRoutines(deviceManager, relayController, [this](const String& routineId, int current, int total, int status) {
    DynamicJsonDocument doc(256);
    doc["type"] = "routine_progress";
    doc["routine_id"] = routineId;
    doc["current_step"] = current;
    doc["total_steps"] = total;
    doc["status"] = status; // 0=running, 1=success, -1=failed
    
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
});
```

### Step 4: Implement on Pi Server (farm-hub)

**Node.js/Express endpoint to send routine to ESP32:**

```javascript
// routes/routines.js
const express = require('express');
const axios = require('axios');
const router = express.Router();

// POST /api/routine/execute
router.post('/execute', async (req, res) => {
    const { deviceId, routineId } = req.body;
    
    // Get device IP from registry
    const device = await getDeviceRegistry(deviceId);
    
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    try {
        // Send routine command to ESP32
        const response = await axios.post(`http://${device.ip}:80/api/routine/execute`, {
            routine_id: routineId,
            timestamp: Date.now()
        }, { timeout: 5000 });
        
        // Store execution record
        await storeExecutionRecord(deviceId, routineId, 'started');
        
        res.json({ 
            success: true, 
            message: 'Routine execution started',
            executionId: `${deviceId}_${routineId}_${Date.now()}`
        });
    } catch (error) {
        console.error(`Failed to execute routine on ${device.ip}:`, error.message);
        res.status(500).json({ 
            error: 'Failed to execute routine on device',
            details: error.message 
        });
    }
});

// GET /api/routine/:deviceId/status
router.get('/:deviceId/status', async (req, res) => {
    const { deviceId } = req.params;
    
    const device = await getDeviceRegistry(deviceId);
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    try {
        const response = await axios.get(`http://${device.ip}:80/api/routine/status`, { 
            timeout: 5000 
        });
        
        res.json(response.data);
    } catch (error) {
        res.status(500).json({ 
            error: 'Failed to get routine status',
            details: error.message 
        });
    }
});

module.exports = router;
```

**Update routines.html to use new endpoints:**

```javascript
// In routines.html JavaScript
const executeRoutine = async (routineId, deviceId) => {
    try {
        const response = await fetch('/api/routine/execute', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ 
                routineId, 
                deviceId 
            })
        });
        
        const result = await response.json();
        
        if (result.success) {
            showNotification('Routine execution started', 'success');
            // Subscribe to WebSocket updates
            subscribeToRoutineUpdates(deviceId, routineId);
        } else {
            showNotification('Failed to start routine', 'error');
        }
    } catch (error) {
        console.error('Error executing routine:', error);
        showNotification('Error executing routine', 'error');
    }
};

// Subscribe to real-time updates from ESP32
const subscribeToRoutineUpdates = (deviceId, routineId) => {
    const ws = new WebSocket(`ws://${device.ip}:80/ws`);
    
    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        
        if (data.type === 'routine_progress') {
            // Update progress bar
            updateProgressBar(
                data.current_step,
                data.total_steps,
                data.status
            );
        } else if (data.type === 'routine_status') {
            console.log('Routine status:', data);
        }
    };
};
```

---

## Communication Sequence

### Routine Execution Flow

```
1. User creates routine in routines.html UI (Pi)
   ↓
2. Pi stores routine definition in database
   ↓
3. User clicks "Execute" button
   ↓
4. Pi sends POST /api/routine/execute to ESP32 with routine ID
   ↓
5. ESP32 receives command via WebManager endpoint
   ↓
6. ESP32 calls RoutineManager::startRoutineByName(routineId)
   ↓
7. ESP32 begins executing routine steps:
   - Turn on relay
   - Wait for condition
   - Monitor sensors
   - Adjust as needed
   ↓
8. ESP32 sends WebSocket progress updates to Pi:
   - Step 1/10 executing
   - Step 2/10 executing
   - etc.
   ↓
9. Pi receives updates and displays in routines.html
   ↓
10. Routine completes (success or failure)
   ↓
11. ESP32 sends final status to Pi
   ↓
12. Pi displays completion and stores execution record
```

---

## Device Confirmation System

The RoutineManager includes a device confirmation system to verify that relays are actually working:

```cpp
struct DeviceConfirmResult {
    String deviceId;              // Device being controlled
    String deviceName;            // Friendly name
    int channel;                  // Relay channel
    bool targetState;             // Desired state (on/off)
    float deltaAmps;              // Amp change when relay triggered
    bool confirmed;               // Was state change confirmed?
};
```

### How it works:

1. **Command**: ESP32 sends command to turn on relay channel 3
2. **Monitor**: Routine checks current sensor (ACS712)
3. **Verify**: If amps increase, confirmation succeeds
4. **Report**: Returns `DeviceConfirmResult` with `confirmed=true`
5. **Failure Callback**: If no amp change detected, triggers failure callback

This ensures routines only proceed if relays actually respond.

---

## Testing the Integration

### Manual Test 1: Direct Routine Trigger

```bash
# SSH to ESP32 or use serial monitor:
# Should trigger RoutineManager::startRoutineByName("test_routine")

curl -X POST http://<ESP32_IP>/api/routine/execute \
  -H "Content-Type: application/json" \
  -d '{"routine_id": "test_routine"}'
```

### Manual Test 2: WebSocket Progress

```javascript
// In browser console on routines.html:
const ws = new WebSocket('ws://<ESP32_IP>/ws');

ws.onmessage = (event) => {
    const data = JSON.parse(event.data);
    console.log('Received:', data);
};

// Execute routine
ws.send(JSON.stringify({
    command: 'execute_routine',
    routine_id: 'test_routine'
}));
```

### Manual Test 3: Multi-Device Coordination

```bash
# Deploy same routine to multiple ESP32s
for device in "esp32_1" "esp32_2" "esp32_3"; do
    curl -X POST http://$(getDeviceIp $device)/api/routine/execute \
      -H "Content-Type: application/json" \
      -d '{"routine_id": "multi_device_routine"}'
done
```

---

## File Structure After Implementation

```
GREENHOUSE_PROJECT/
├── 260125-185311-esp32dev/
│   ├── src/main.cpp              (unchanged)
│   ├── include/
│   │   ├── WebManager.h          (add /api/routine/* endpoints)
│   │   ├── RoutineManager.h      (already present)
│   │   ├── RoutineStorage.h      ← NEW (routine persistence)
│   │   └── Secrets.h             (unchanged)
│   └── data/
│       └── index.html            (unchanged)
│
└── raspberry-pi/
    └── routes/
        ├── routines.js           ← UPDATE (add execute/status)
        └── devices.js            (unchanged)
```

---

## Success Criteria

You'll know the integration is working when:

✅ Can trigger routine from Pi routines.html
✅ Routine executes on ESP32 in real-time
✅ Progress updates visible in Pi UI
✅ ESP32 reports relay confirmation
✅ Multiple ESP32s can run routines independently
✅ Routine history saved on Pi server
✅ Failed routines generate alerts

---

## Next: Alert System Integration

Once routine execution is working, follow [ALERT_SYSTEM_INTEGRATION.md](ALERT_SYSTEM_INTEGRATION.md) to complete the alert reporting system.
