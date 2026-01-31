#include "WiFiProvisioning.h"
#include "Secrets.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>

// Static member definitions
WiFiProvisioning::WiFiState WiFiProvisioning::currentState = WiFiProvisioning::STATE_INIT;
WiFiProvisioning::DeviceType WiFiProvisioning::deviceType = WiFiProvisioning::GENERIC;
unsigned long WiFiProvisioning::stateStartTime = 0;
uint8_t WiFiProvisioning::connectionAttempts = 0;
unsigned long WiFiProvisioning::lastNetworkStatusReport = 0;
unsigned long WiFiProvisioning::lastOTACheck = 0;

// Global objects for AP mode
static DNSServer dnsServer;
static AsyncWebServer apServer(80);
static String targetSSID = "";
static String targetPassword = "";
static bool configReceived = false;

void WiFiProvisioning::begin(DeviceType type) {
    deviceType = type;
    currentState = STATE_INIT;
    connectionAttempts = 0;
    stateStartTime = millis();
    
    Serial.printf("[WiFi-Prov] Initializing provisioning for device type: %d\n", deviceType);
    
    // Setup WiFi modes
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoConnect(false);
    WiFi.setAutoReconnect(false);
    
    transitionToConnecting();
}

void WiFiProvisioning::update() {
    unsigned long elapsedMs = millis() - stateStartTime;
    
    switch (currentState) {
        case STATE_INIT:
            transitionToConnecting();
            break;
            
        case STATE_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi-Prov] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());
                transitionToHandshake();
            } else if (elapsedMs > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.printf("[WiFi-Prov] WiFi timeout after %ld attempts\n", connectionAttempts);
                if (connectionAttempts < MAX_ATTEMPTS) {
                    // Try next credential source
                    connectionAttempts++;
                    stateStartTime = millis();
                    // In real usage, would try different SSID/pass combinations
                    // For now, retry same credentials
                } else {
                    // All attempts failed, enter AP mode
                    transitionToAPMode();
                }
            }
            break;
            
        case STATE_HANDSHAKE: {
            // Load Pi IP from config
            Preferences prefs;
            prefs.begin("gh-config", true);
            String piIP = prefs.getString("pi", "");
            prefs.end();
            
            if (piIP.length() > 0) {
                if (handshakeWithPi(piIP.c_str()) && notifyPi(piIP.c_str())) {
                    Serial.println("[WiFi-Prov] Handshake successful!");
                    transitionToReady();
                } else if (elapsedMs > HANDSHAKE_TIMEOUT_MS) {
                    Serial.println("[WiFi-Prov] Handshake timeout, entering AP mode");
                    transitionToAPMode();
                }
            } else {
                // No Pi configured, assume local network OK
                Serial.println("[WiFi-Prov] No Pi configured, assuming direct network");
                transitionToReady();
            }
            break;
        }
            
        case STATE_AP_MODE:
            handleAPModeUpdate();
            break;
            
        case STATE_READY:
            // Periodically verify connection
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi-Prov] Lost WiFi connection!");
                transitionToConnecting();
            }
            break;
            
        case STATE_FAILED:
            // Retry after delay
            if (elapsedMs > RETRY_DELAY_MS * 5) {
                Serial.println("[WiFi-Prov] Retrying provisioning...");
                transitionToConnecting();
            }
            break;
    }
}

String WiFiProvisioning::getDeviceID() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    char deviceID[32];
    snprintf(deviceID, sizeof(deviceID), "ESP32_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return String(deviceID);
}

bool WiFiProvisioning::handshakeWithPi(const char* piAddress) {
    if (!piAddress || strlen(piAddress) == 0) return false;
    
    HTTPClient http;
    String url = String("http://") + piAddress + ":3000/api/device/handshake";
    
    Serial.printf("[WiFi-Prov] Handshaking with Pi at %s\n", url.c_str());
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["device_id"] = getDeviceID();
    doc["device_type"] = deviceType;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == 200);
    
    if (success) {
        String response = http.getString();
        Serial.printf("[WiFi-Prov] Pi response: %s\n", response.c_str());
    } else {
        Serial.printf("[WiFi-Prov] Handshake failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

bool WiFiProvisioning::notifyPi(const char* piAddress) {
    if (!piAddress || strlen(piAddress) == 0) return false;
    
    HTTPClient http;
    String url = String("http://") + piAddress + ":3000/api/device/register";
    
    Serial.printf("[WiFi-Prov] Registering device with Pi at %s\n", url.c_str());
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    JsonDocument doc;
    doc["device_id"] = getDeviceID();
    doc["device_type"] = deviceType;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["firmware_version"] = "1.0.0";  // TODO: Make configurable
    doc["status"] = "ready";
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == 200);
    
    if (!success) {
        Serial.printf("[WiFi-Prov] Device registration failed, HTTP code: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

bool WiFiProvisioning::reportNetworkStatus(const char* piAddress) {
    if (!piAddress || strlen(piAddress) == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    
    HTTPClient http;
    String url = String("http://") + piAddress + ":3000/api/device/network-status";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    // Get OTA status from preferences
    Preferences prefs;
    prefs.begin("gh-config", true);
    String lastOTAStatus = prefs.getString("ota_status", "idle");
    String lastOTATime = prefs.getString("ota_time", "never");
    prefs.end();
    
    JsonDocument doc;
    doc["device_id"] = getDeviceID();
    doc["device_name"] = "Greenhouse_Main";  // TODO: Make configurable
    doc["device_type"] = deviceType;
    doc["ip_address"] = WiFi.localIP().toString();
    doc["mac_address"] = WiFi.macAddress();
    doc["rssi"] = WiFi.RSSI();
    doc["hostname"] = WiFi.getHostname();
    doc["uptime_ms"] = millis();
    doc["firmware_version"] = "2.0.0";  // TODO: Make configurable
    doc["free_heap"] = ESP.getFreeHeap();
    doc["total_heap"] = ESP.getHeapSize();
    doc["heap_usage_percent"] = ESP.getHeapSize() > 0 ? (int)((ESP.getFreeHeap() * 100) / ESP.getHeapSize()) : 0;
    doc["gateway"] = WiFi.gatewayIP().toString();
    doc["subnet_mask"] = WiFi.subnetMask().toString();
    
    // Add OTA status information
    doc["ota_status"] = lastOTAStatus;      // idle, checking, updating, success, failed
    doc["ota_last_attempt"] = lastOTATime;
    
    // Add DNS servers (use standard format)
    JsonArray dnsArray = doc["dns_servers"].to<JsonArray>();
    dnsArray.add("8.8.8.8");
    dnsArray.add("8.8.4.4");
    
    String payload;
    serializeJson(doc, payload);
    
    int httpCode = http.POST(payload);
    bool success = (httpCode == 200);
    
    if (!success) {
        Serial.printf("[WiFi-Prov] Network status report failed, HTTP code: %d\n", httpCode);
    } else {
        Serial.printf("[WiFi-Prov] Network status reported - RSSI: %d dBm, Heap: %u bytes\n", 
                     WiFi.RSSI(), ESP.getFreeHeap());
    }
    
    http.end();
    return success;
}

bool WiFiProvisioning::checkAndDownloadOTA(const char* piAddress) {
    if (!piAddress || strlen(piAddress) == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    
    HTTPClient http;
    String url = String("http://") + piAddress + ":3000/api/device/ota/" + getDeviceID();
    
    Serial.printf("[OTA] Checking for firmware update from: %s\n", url.c_str());
    
    // Update status in preferences
    Preferences prefs;
    prefs.begin("gh-config", false);
    prefs.putString("ota_status", "checking");
    prefs.putString("ota_time", String(millis() / 1000));
    prefs.end();
    
    http.begin(url);
    http.setRedirectLimit(1);
    http.setConnectTimeout(5000);
    http.setTimeout(OTA_DOWNLOAD_TIMEOUT_MS);  // Use configurable timeout
    
    int httpCode = http.GET();
    
    if (httpCode == 200) {
        int contentLength = http.getSize();
        
        if (contentLength > 0 && contentLength < ESP.getFreeSketchSpace()) {
            Serial.printf("[OTA] Firmware available, size: %d bytes, free sketch space: %d bytes\n", 
                         contentLength, ESP.getFreeSketchSpace());
            
            // Update status to updating
            prefs.begin("gh-config", false);
            prefs.putString("ota_status", "updating");
            prefs.end();
            
            // Check if Update.begin can handle the firmware size
            if (!Update.begin(contentLength)) {
                Serial.printf("[OTA] Update.begin() failed, error: %s\n", Update.errorString());
                
                // Mark update as failed
                prefs.begin("gh-config", false);
                prefs.putString("ota_status", "failed");
                prefs.end();
                
                http.end();
                return false;
            }
            
            // Download and flash the firmware
            WiFiClient* stream = http.getStreamPtr();
            size_t written = Update.writeStream(*stream);
            
            if (written == contentLength) {
                Serial.println("[OTA] Firmware download complete, finalizing update...");
                
                if (Update.end(true)) {
                    Serial.println("[OTA] Update successful! Marking as success and rebooting...");
                    
                    // Mark as successful before reboot
                    prefs.begin("gh-config", false);
                    prefs.putString("ota_status", "success");
                    prefs.end();
                    
                    http.end();
                    
                    // Wait a moment for settings to sync
                    delay(1000);
                    
                    // Device will reboot automatically
                    return true;
                } else {
                    Serial.printf("[OTA] Update.end() failed, error: %s\n", Update.errorString());
                    
                    // Mark as failed - device will rollback on next reboot
                    prefs.begin("gh-config", false);
                    prefs.putString("ota_status", "failed");
                    prefs.end();
                }
            } else {
                Serial.printf("[OTA] Written size (%u) != Content-Length (%d)\n", written, contentLength);
                
                prefs.begin("gh-config", false);
                prefs.putString("ota_status", "failed");
                prefs.end();
            }
        } else if (contentLength >= ESP.getFreeSketchSpace()) {
            Serial.printf("[OTA] Firmware too large! Size: %d, Available: %d\n", 
                         contentLength, ESP.getFreeSketchSpace());
            
            prefs.begin("gh-config", false);
            prefs.putString("ota_status", "failed");
            prefs.putString("ota_error", "firmware_too_large");
            prefs.end();
        } else {
            Serial.println("[OTA] No firmware available (content length 0)");
            
            prefs.begin("gh-config", false);
            prefs.putString("ota_status", "idle");
            prefs.end();
        }
    } else if (httpCode == 404) {
        Serial.println("[OTA] No updates available on server (404)");
        
        prefs.begin("gh-config", false);
        prefs.putString("ota_status", "idle");
        prefs.end();
    } else {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        
        prefs.begin("gh-config", false);
        prefs.putString("ota_status", "failed");
        prefs.end();
    }
    
    http.end();
    return false;
}

bool WiFiProvisioning::tryConnection(const char* ssid, const char* password) {
    Serial.printf("[WiFi-Prov] Attempting connection to: %s\n", ssid);
    
    WiFi.disconnect(false);  // Disconnect without turning off radio
    vTaskDelay(pdMS_TO_TICKS(100));
    
    WiFi.begin(ssid, password);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && 
           (millis() - startTime) < WIFI_CONNECT_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (millis() - startTime > 5000 && 
            (millis() - startTime) % 2000 == 0) {
            Serial.print(".");
        }
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi-Prov] Connected! IP: %s, RSSI: %d\n", 
                     WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    }
    
    Serial.println("\n[WiFi-Prov] Connection timeout");
    return false;
}

bool WiFiProvisioning::connectToSavedNetwork() {
    Preferences prefs;
    prefs.begin("gh-config", true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("pass", "");
    prefs.end();
    
    if (ssid.length() == 0) {
        Serial.println("[WiFi-Prov] No saved network");
        return false;
    }
    
    return tryConnection(ssid.c_str(), password.c_str());
}

bool WiFiProvisioning::connectToDefaultNetwork() {
    return tryConnection(DEFAULT_SSID, DEFAULT_PASS);
}

void WiFiProvisioning::startAPMode() {
    Serial.println("[WiFi-Prov] Starting AP mode...");
    
    String apSSID = "Greenhouse_" + getDeviceID().substring(7);  // Short device ID
    
    if (WiFi.softAP(apSSID.c_str(), AP_PASS)) {
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        
        Serial.printf("[WiFi-Prov] AP Started: %s @ %s\n", apSSID.c_str(), apIP.toString().c_str());
        Serial.printf("[WiFi-Prov] Connect to AP and navigate to http://192.168.4.1\n");
        
        // Setup DNS
        dnsServer.start(53, "*", apIP);
        
        // Setup web server for configuration
        apServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            String html = R"(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Greenhouse Device Setup</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #0a0e1a;
            color: #f8fafc;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
        }
        .container {
            background: #151b2e;
            border-radius: 12px;
            padding: 30px;
            max-width: 400px;
            width: 100%;
            box-shadow: 0 10px 40px rgba(0,0,0,0.3);
            border: 1px solid rgba(79,124,255,0.2);
        }
        h1 {
            color: #4f7cff;
            margin-top: 0;
            text-align: center;
            font-size: 1.8rem;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            font-weight: 600;
            color: #cbd5e1;
        }
        input, select {
            width: 100%;
            padding: 12px;
            border: 1px solid #334155;
            border-radius: 8px;
            background: #0f172a;
            color: #f8fafc;
            font-size: 1rem;
            box-sizing: border-box;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #4f7cff;
            box-shadow: 0 0 8px rgba(79,124,255,0.3);
        }
        button {
            width: 100%;
            padding: 12px;
            background: linear-gradient(135deg, #4f7cff, #7c3aed);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 1rem;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s;
        }
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 20px rgba(79,124,255,0.4);
        }
        .message {
            margin-top: 20px;
            padding: 12px;
            border-radius: 8px;
            text-align: center;
            display: none;
        }
        .success {
            background: rgba(0,217,165,0.2);
            color: #00d9a5;
            border: 1px solid rgba(0,217,165,0.4);
        }
        .error {
            background: rgba(255,71,87,0.2);
            color: #ff4757;
            border: 1px solid rgba(255,71,87,0.4);
        }
        .device-info {
            background: rgba(79,124,255,0.1);
            padding: 12px;
            border-radius: 8px;
            margin-bottom: 20px;
            border: 1px solid rgba(79,124,255,0.2);
        }
        .device-info p {
            margin: 6px 0;
            font-size: 0.9rem;
            color: #cbd5e1;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌱 Device Setup</h1>
        <div class="device-info" id="deviceInfo">
            <p><strong>Device ID:</strong> <span id="deviceId">Loading...</span></p>
            <p><strong>Mode:</strong> Access Point</p>
        </div>
        <form id="configForm">
            <div class="form-group">
                <label for="ssid">WiFi Network (SSID)</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter WiFi name" required>
            </div>
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" placeholder="Enter password" required>
            </div>
            <div class="form-group">
                <label for="piIp">Pi Station IP (Optional)</label>
                <input type="text" id="piIp" name="piIp" placeholder="Enter Pi IP">
            </div>
            <div class="form-group">
                <label for="deviceType">Device Type</label>
                <select id="deviceType" name="deviceType" required>
                    <option value="1">Greenhouse</option>
                    <option value="2">Chicken Coop</option>
                    <option value="3">Grow Box</option>
                    <option value="4">Humidity Station</option>
                    <option value="255">Generic</option>
                </select>
            </div>
            <button type="submit">Apply & Restart</button>
        </form>
        <div class="message" id="message"></div>
    </div>

    <script>
        document.getElementById('configForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            
            const config = {
                ssid: document.getElementById('ssid').value,
                pass: document.getElementById('password').value,
                pi: document.getElementById('piIp').value,
                deviceType: parseInt(document.getElementById('deviceType').value)
            };
            
            try {
                const response = await fetch('/api/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(config)
                });
                
                const result = await response.json();
                const msgEl = document.getElementById('message');
                
                if (result.success) {
                    msgEl.className = 'message success';
                    msgEl.textContent = '✓ Configuration saved! Device will restart...';
                    msgEl.style.display = 'block';
                    setTimeout(() => window.location.reload(), 3000);
                } else {
                    msgEl.className = 'message error';
                    msgEl.textContent = '✗ Error: ' + (result.error || 'Unknown error');
                    msgEl.style.display = 'block';
                }
            } catch (err) {
                const msgEl = document.getElementById('message');
                msgEl.className = 'message error';
                msgEl.textContent = '✗ Failed to save config';
                msgEl.style.display = 'block';
            }
        });
        
        // Try to get device info
        fetch('/api/device-info')
            .then(r => r.json())
            .then(d => {
                document.getElementById('deviceId').textContent = d.device_id || 'Unknown';
            })
            .catch(() => {
                document.getElementById('deviceId').textContent = 'Unknown';
            });
    </script>
</body>
</html>
            )";
            request->send(200, "text/html", html);
        });
        
        apServer.on("/api/device-info", HTTP_GET, [](AsyncWebServerRequest *request) {
            JsonDocument doc;
            doc["device_id"] = getDeviceID();
            doc["device_type"] = deviceType;
            String response;
            serializeJson(doc, response);
            request->send(200, "application/json", response);
        });
        
        apServer.on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {}, nullptr, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            deserializeJson(doc, (const char*)data);
            
            String ssid = doc["ssid"] | "";
            String pass = doc["pass"] | "";
            String pi = doc["pi"] | "";
            int type = doc["deviceType"] | 255;
            
            if (ssid.length() == 0 || pass.length() == 0) {
                JsonDocument resp;
                resp["success"] = false;
                resp["error"] = "SSID and password required";
                String response;
                serializeJson(resp, response);
                request->send(400, "application/json", response);
                return;
            }
            
            // Save to NVS
            Preferences prefs;
            prefs.begin("gh-config", false);
            prefs.putString("ssid", ssid);
            prefs.putString("pass", pass);
            if (pi.length() > 0) prefs.putString("pi", pi);
            prefs.putInt("deviceType", type);
            prefs.end();
            
            JsonDocument resp;
            resp["success"] = true;
            resp["message"] = "Configuration saved. Restarting...";
            String response;
            serializeJson(resp, response);
            request->send(200, "application/json", response);
            
            // Schedule restart
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP.restart();
        });
        
        apServer.onNotFound([](AsyncWebServerRequest *request) {
            request->redirect("/");
        });
        
        apServer.begin();
    } else {
        Serial.println("[WiFi-Prov] Failed to start AP");
        transitionToFailed();
    }
}

void WiFiProvisioning::handleAPModeUpdate() {
    dnsServer.processNextRequest();
}

// State transition functions
void WiFiProvisioning::transitionToConnecting() {
    currentState = STATE_CONNECTING;
    connectionAttempts = 0;
    stateStartTime = millis();
    
    // Try saved network first
    if (!connectToSavedNetwork()) {
        // Try default network
        if (!connectToDefaultNetwork()) {
            // Both failed, will timeout and go to AP mode
            Serial.println("[WiFi-Prov] All WiFi attempts failed");
        }
    }
}

void WiFiProvisioning::transitionToHandshake() {
    currentState = STATE_HANDSHAKE;
    stateStartTime = millis();
    Serial.println("[WiFi-Prov] Transitioning to handshake phase");
}

void WiFiProvisioning::transitionToReady() {
    currentState = STATE_READY;
    stateStartTime = millis();
    Serial.println("[WiFi-Prov] Device is ready for operation!");
}

void WiFiProvisioning::transitionToAPMode() {
    currentState = STATE_AP_MODE;
    stateStartTime = millis();
    
    // Stop WiFi attempts
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    startAPMode();
}

void WiFiProvisioning::transitionToFailed() {
    currentState = STATE_FAILED;
    stateStartTime = millis();
    Serial.println("[WiFi-Prov] Provisioning failed!");
}
