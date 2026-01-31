#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Secrets.h"
#include "WiFiProvisioning.h"

// ============ GLOBAL CONFIG ============
char piIp[40] = "";
WiFiProvisioning wifiProv;

// ============ DEVICE REGISTRATION ============
String lastRegisteredIP = "";
unsigned long lastIPCheck = 0;
static constexpr unsigned long IP_CHECK_INTERVAL = 30000UL;  // Check every 30 seconds
static constexpr unsigned long IP_REGISTRATION_TIMEOUT = 3600000UL;  // Re-register every hour

void registerDeviceWithPi() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (strlen(piIp) < 5) return;

    String hostname = WiFi.getHostname();
    String ip = WiFi.localIP().toString();

    lastRegisteredIP = ip;
    lastIPCheck = millis();

    WiFiClient client;
    client.setTimeout(2000);
    HTTPClient http;
    http.setTimeout(3000);

    char url[64];
    snprintf(url, sizeof(url), "http://%s:3000/api/device/register", piIp);

    JsonDocument doc;
    doc["device_id"] = hostname;
    doc["hostname"] = hostname;
    doc["ip_address"] = ip;
    doc["device_type"] = "greenhouse";

    String payload;
    serializeJson(doc, payload);

    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(payload);

    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("[DEVICE] ✓ Registered with Pi: %s at %s\n", hostname.c_str(), ip.c_str());
    } else {
        Serial.printf("[DEVICE] ✗ Registration failed: HTTP %d\n", httpCode);
    }
    http.end();
}

void verifyDeviceRegistration() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (strlen(piIp) < 5) return;

    String hostname = WiFi.getHostname();
    String currentIP = WiFi.localIP().toString();

    WiFiClient client;
    client.setTimeout(2000);
    HTTPClient http;
    http.setTimeout(3000);

    char url[96];
    snprintf(url, sizeof(url), "http://%s:3000/api/device/verify/%s", piIp, hostname.c_str());

    http.begin(client, url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String response = http.getString();
        JsonDocument doc;
        deserializeJson(doc, response);
        
        String registeredIP = doc["ip_address"].as<String>();
        if (registeredIP == currentIP) {
            Serial.printf("[DEVICE] ✓ Verified: %s at %s\n", hostname.c_str(), currentIP.c_str());
            lastRegisteredIP = currentIP;
            lastIPCheck = millis();
        } else {
            Serial.printf("[DEVICE] ⚠ IP mismatch: %s → %s. Re-registering...\n", registeredIP.c_str(), currentIP.c_str());
            registerDeviceWithPi();
        }
    } else if (httpCode == 404) {
        Serial.printf("[DEVICE] ⚠ Not in database. Registering...\n");
        registerDeviceWithPi();
    } else {
        Serial.printf("[DEVICE] ✗ Verification failed: HTTP %d\n", httpCode);
    }

    http.end();
}

void deviceRegistrationTask(void *pvParameters) {
    const unsigned long CHECK_INTERVAL = 30000;  // 30 seconds
    unsigned long lastCheck = 0;

    for (;;) {
        if (millis() - lastCheck >= CHECK_INTERVAL) {
            lastCheck = millis();
            verifyDeviceRegistration();
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ============ SETUP ============
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n========================================");
    Serial.println("GREENHOUSE - Device Registration System");
    Serial.println("========================================");

    // Initialize WiFi Provisioning
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("greenhouse");
    
    Serial.println("\n[SETUP] Initializing WiFi...");
    WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);

    // Wait for WiFi connection
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[SETUP] ✓ WiFi connected!\n");
        Serial.printf("  IP Address: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("  Hostname: %s\n", WiFi.getHostname());

        // Set Pi IP
        strncpy(piIp, DEFAULT_PI_IP, sizeof(piIp) - 1);
        Serial.printf("  Pi Server: %s:3000\n", piIp);

        // Register immediately on startup
        Serial.println("\n[SETUP] Registering device...");
        registerDeviceWithPi();

        // Start background device registration verification task
        Serial.println("[SETUP] Starting device verification task (Core 1, Priority 0)...");
        xTaskCreatePinnedToCore(
            deviceRegistrationTask,
            "DeviceRegTask",
            4096,
            NULL,
            0,      // Priority 0 (lowest)
            NULL,
            1       // Core 1 (network)
        );

        Serial.println("\n[SETUP] ✓ Initialization complete!");
    } else {
        Serial.printf("\n[SETUP] ✗ WiFi connection failed!\n");
    }

    Serial.println("========================================\n");
}

// ============ LOOP ============
void loop() {
    // Every 10 seconds, print status
    static unsigned long lastStatusPrint = 0;
    unsigned long now = millis();

    if (now - lastStatusPrint >= 10000) {
        lastStatusPrint = now;
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[LOOP] ✓ WiFi connected | IP: %s | Registered: %s\n",
                         WiFi.localIP().toString().c_str(),
                         lastRegisteredIP.length() > 0 ? lastRegisteredIP.c_str() : "pending");
        } else {
            Serial.printf("[LOOP] ✗ WiFi disconnected (status: %d)\n", WiFi.status());
        }
    }

    delay(1000);
}
