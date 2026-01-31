#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <nvs_flash.h>
#include <esp_task_wdt.h>
#include <map>
#include <atomic>
#include <tuple>
#include "time.h"
#include "esp_sntp.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "Secrets.h"
#include "WiFiProvisioning.h"
#include "CurrentSensorManager.h"
#include "RelayController.h"
#include "WebManager.h"
#include "OTA_Manager.h"
#include "DeviceManager.h"
#include "RoutineManager.h"
#include "SDManager.h"
#include "AlertManager.h"

const byte DNS_PORT = 53;
DNSServer dnsServer;
CurrentSensorManager currentSensor;  // Single main-line current sensor
RelayController relays;
WebManager web(80);
DeviceManager deviceMgr;
RoutineManager routineMgr;
SDManager sdCard;
AlertManager alertMgr;  // WhatsApp/Telegram alert system (self-contained NVS storage)

// Current sensor configuration
static constexpr int CURRENT_SENSOR_PIN = 34;  // GPIO34 (ADC1_CH6) - ACS712 signal pin

// DS18B20 Temperature Sensors (OneWire bus)
static constexpr int ONEWIRE_PIN = 4;  // GPIO4 for OneWire bus
OneWire oneWire(ONEWIRE_PIN);
DallasTemperature tempSensors(&oneWire);
DeviceAddress sensorAddresses[6];  // Store up to 6 sensor addresses
int sensorCount = 0;

// Config State - use char arrays to reduce heap fragmentation
bool useProxy = false;
char piIp[40] = "";
char savedSSID[33] = "";
float currentTemperature = 0.0f;
bool tempRising = true;
bool isAPMode = true;
volatile bool scanRequested = false;
unsigned long lastWiFiCheck = 0;
uint8_t wifiReconnectAttempts = 0;  // uint8_t saves RAM
static constexpr uint8_t MAX_WIFI_RECONNECT_ATTEMPTS = 5;
static constexpr unsigned long WIFI_CHECK_INTERVAL = 10000;

// Proxy connection state
volatile bool proxyConnected = false; // True if Pi WebSocket is up
unsigned long lastProxyPing = 0;
static constexpr unsigned long PROXY_TIMEOUT_MS = 15000; // 15s timeout

// IP address monitoring for DHCP changes
String lastRegisteredIP = "";
unsigned long lastIPCheck = 0;
static constexpr unsigned long IP_CHECK_INTERVAL = 30000UL; // Check every 30 seconds
static constexpr unsigned long IP_REGISTRATION_TIMEOUT = 3600000UL; // Re-register every hour anyway

// Time Config
static constexpr char NTP_SERVER_DEFAULT[] PROGMEM = "pool.ntp.org";
char ntpServer[48] = "pool.ntp.org";
long gmtOffset_sec = 0;
int daylightOffset_sec = 0;

// Location Config - fixed arrays reduce heap fragmentation
char cfgLat[16] = "";
char cfgLon[16] = "";
char cfgCity[48] = "";
char cfgRegion[48] = "";
char cfgUnit[2] = "c";
float cfgAmpThreshold = 0.25f;
unsigned long lastWeatherUpdate = 0;
unsigned long lastWeatherRequest = 0;  // Debounce for weather request triggers
constexpr unsigned long WEATHER_DEBOUNCE_MS = 5000;  // Minimum 5 seconds between weather triggers
unsigned long lastLocationSync = 0;
unsigned long lastSettingsSync = 0;
static constexpr unsigned long SETTINGS_SYNC_INTERVAL = 300000UL; // Sync settings every 5 minutes

// Timing Constants - static constexpr for compile-time optimization
static constexpr unsigned long LOCATION_SYNC_INTERVAL = 3600000UL;
static constexpr unsigned long WEATHER_UPDATE_INTERVAL = 1800000UL;
static constexpr unsigned long SENSOR_FRESHNESS_MS = 60000UL;
static constexpr unsigned long ROUTINE_CHECK_INTERVAL = 60000UL;

// Routine trigger state
unsigned long lastRoutineCheck = 0;
float lastWeatherTemp = 0.0f;

// Weather caching system
String cachedWeatherJson = "";         // Cached weather data JSON
unsigned long cachedWeatherTimestamp = 0; // Unix timestamp when cached
unsigned long pendingWeatherRefresh = 0;  // millis() time to trigger delayed weather refresh (0 = none)
bool weatherCacheStale = true;            // True if cache needs refresh
volatile bool pendingCacheBroadcast = false; // Flag to broadcast cached weather from SyncTask

// Helper to update NVS if value changed
void updateNvsString(const char* key, const String& value, Preferences& prefs) {
    String current = prefs.getString(key, "");
    if (current != value) {
        prefs.putString(key, value);
        Serial.printf("[CFG] Updated %s in NVS: %s\n", key, value.c_str());
    }
}

void syncSettingsFromPi() {
    if(WiFi.status() != WL_CONNECTED) return;
    if(strlen(piIp) < 5) return; // No Pi IP configured
    
    // Check interval
    if(lastSettingsSync != 0 && (millis() - lastSettingsSync) < SETTINGS_SYNC_INTERVAL) return;
    
    Serial.printf("[Settings] Syncing from Pi API (http://%s:3000/api/settings)...\n", piIp);
    
    WiFiClient client;
    client.setTimeout(2000);
    HTTPClient http;
    http.setTimeout(3000);
    
    char url[64];
    snprintf(url, sizeof(url), "http://%s:3000/api/settings", piIp);
    
    http.begin(client, url);
    int httpCode = http.GET();
    
    if(httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        
        if(!error) {
            bool configChanged = false;
            Preferences prefs;
            prefs.begin("gh-config", false);
            
            // 1. Parse Location
            if(doc.containsKey("location")) {
                JsonObject loc = doc["location"];
                
                // Lat/Lon
                if(loc.containsKey("lat") && loc.containsKey("lon")) {
                    String sLat = loc["lat"].as<String>();
                    String sLon = loc["lon"].as<String>();
                    
                    if(sLat != String(cfgLat) || sLon != String(cfgLon)) {
                        strncpy(cfgLat, sLat.c_str(), sizeof(cfgLat)-1);
                        strncpy(cfgLon, sLon.c_str(), sizeof(cfgLon)-1);
                        updateNvsString("lat", sLat, prefs);
                        updateNvsString("lon", sLon, prefs);
                        lastWeatherUpdate = 0; // Force weather refresh on location change
                        configChanged = true;
                    }
                }
                
                // Timezone (if needed in future)
                // Accessing nested "address" for city/region inference
                if(loc.containsKey("address")) {
                     String fullAddr = loc["address"].as<String>();
                     // Simple heuristic: First part is city, last part is Country/Region
                     int firstComma = fullAddr.indexOf(',');
                     if(firstComma > 0) {
                        String sCity = fullAddr.substring(0, firstComma);
                        if(sCity != String(cfgCity) && sCity.length() < 48) {
                            strncpy(cfgCity, sCity.c_str(), sizeof(cfgCity)-1);
                            updateNvsString("city", sCity, prefs);
                            configChanged = true;
                        }
                     }
                }
            }
            
            // 2. Parse Units
            if(doc.containsKey("units")) {
                JsonObject units = doc["units"];
                if(units.containsKey("temp")) {
                    String sUnit = units["temp"].as<String>();
                    String currentUnit(cfgUnit);
                    // Compare case-insensitive 'c'/'f' vs 'C'/'F'
                    if(!currentUnit.equalsIgnoreCase(sUnit)) {
                        strncpy(cfgUnit, sUnit.c_str(), sizeof(cfgUnit)-1);
                        updateNvsString("unit", sUnit, prefs);
                        lastWeatherUpdate = 0; // Force weather refresh to get new unit
                        configChanged = true;
                    }
                }
            }
            
            prefs.end();
            lastSettingsSync = millis();
            
            if(configChanged) {
                Serial.println("[Settings] Configuration updated from Server.");
            } else {
                Serial.println("[Settings] Configuration is up to date.");
            }
            
        } else {
            Serial.println("[Settings] JSON Parse Error");
        }
    } else {
        Serial.printf("[Settings] HTTP Error: %d\n", httpCode);
    }
    http.end();
}

// Register this device with the Pi server
void registerDeviceWithPi() {
    if(WiFi.status() != WL_CONNECTED) return;
    if(strlen(piIp) < 5) return; // No Pi IP configured
    
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
    
    if(httpCode == HTTP_CODE_OK) {
        Serial.printf("[DEVICE] Registered with Pi: %s at %s\n", hostname.c_str(), ip.c_str());
    } else {
        Serial.printf("[DEVICE] Registration failed: HTTP %d\n", httpCode);
    }
    http.end();
}

// Check if IP address changed and re-register if needed
void checkIPAddressChange() {
    if(WiFi.status() != WL_CONNECTED) return;
    if(strlen(piIp) < 5) return; // No Pi IP configured
    
    // Check every 30 seconds, or force re-register every hour
    if(lastIPCheck != 0 && (millis() - lastIPCheck) < IP_CHECK_INTERVAL) return;
    
    String currentIP = WiFi.localIP().toString();
    
    // Re-register if IP changed or if it's been an hour
    if(currentIP != lastRegisteredIP || (lastIPCheck != 0 && (millis() - lastIPCheck) > IP_REGISTRATION_TIMEOUT)) {
        if(currentIP != lastRegisteredIP) {
            Serial.printf("[DEVICE] IP address changed from %s to %s, re-registering...\n", 
                lastRegisteredIP.c_str(), currentIP.c_str());
        }
        registerDeviceWithPi();
    } else {
        lastIPCheck = millis();
    }
}

// Verify device registration with Pi server (timer-based check)
void verifyDeviceRegistration() {
    if(WiFi.status() != WL_CONNECTED) return;
    if(strlen(piIp) < 5) {
        Serial.println("[DEVICE] Pi IP not configured, skipping verification");
        return;
    }
    
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
    
    if(httpCode == 200) {
        // Device found - verify IP matches
        String response = http.getString();
        JsonDocument doc;
        deserializeJson(doc, response);
        
        String registeredIP = doc["device"]["ip"].as<String>();
        
        if(registeredIP == currentIP) {
            Serial.printf("[DEVICE] ✓ Verified: %s at %s\n", hostname.c_str(), currentIP.c_str());
            lastRegisteredIP = currentIP;
            lastIPCheck = millis();
        } else {
            Serial.printf("[DEVICE] IP mismatch: registered=%s, current=%s. Re-registering...\n", 
                registeredIP.c_str(), currentIP.c_str());
            registerDeviceWithPi();
        }
    } else if(httpCode == 404) {
        // Device not found in database - register it
        Serial.printf("[DEVICE] Not found in database (HTTP 404). Registering...\n");
        registerDeviceWithPi();
    } else {
        Serial.printf("[DEVICE] Verification failed: HTTP %d\n", httpCode);
    }
    
    http.end();
}

// FreeRTOS Task: Periodic device registration verification (low priority)
void deviceRegistrationTask(void *pvParameters) {
    const unsigned long CHECK_INTERVAL = 30000; // 30 seconds
    unsigned long lastCheck = 0;
    
    for(;;) {
        if(millis() - lastCheck >= CHECK_INTERVAL) {
            lastCheck = millis();
            verifyDeviceRegistration();
        }
        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds if timer expired
    }
}

void fetchWeather() {
    // Only fetch weather if NOT connected to Pi proxy
    if (proxyConnected) {
        Serial.println(F("[Weather] ⏸️  PAUSED: Pi proxy is connected. UI will request weather from Pi API."));
        // When Pi is connected, UI should get weather from http://<pi>/api/weather
        // This allows ESP32 to focus all resources on running the backend systems
        return;
    }
    // Ensure we have location
    if(strlen(cfgLat) < 2 || strlen(cfgLon) < 2) {
        Serial.println(F("[Weather] No coordinates set."));
        web.broadcastStatus(F("{\"type\":\"weather\",\"data\":{\"valid\":false}}"));
        return;
    }
    
    if(WiFi.status() != WL_CONNECTED) {
        web.broadcastStatus(F("{\"type\":\"weather\",\"data\":{\"valid\":false}}"));
        return;
    }
    
    Serial.println(F("[Weather] Fetching..."));
    
    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(5000); // Reduced to 5s timeout to prevent freezing
    
    HTTPClient http;
    http.setTimeout(5000);
    // Build URL with static buffer - much faster than String concatenation
    static char urlBuf[384];
    const char* tempUnit = (cfgUnit[0] == 'f' || cfgUnit[0] == 'F') ? "fahrenheit" : "celsius";
    snprintf(urlBuf, sizeof(urlBuf),
        "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
        "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day,apparent_temperature"
        "&hourly=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min"
        "&forecast_days=1&temperature_unit=%s&wind_speed_unit=kmh&timezone=auto",
        cfgLat, cfgLon, tempUnit);
    
    http.begin(client, urlBuf);
    const int httpCode = http.GET();
    
    if(httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.printf("[Weather] Response size: %d bytes\n", payload.length());
        Serial.printf("[Weather] First 200 chars: %s\n", payload.substring(0, 200).c_str());
        
        JsonDocument doc;  // Use ArduinoJson 7 auto-sizing
        DeserializationError error = deserializeJson(doc, payload);
        yield(); // Feed watchdog
        
        if(!error) {
            // Update Timezone from location
            const long secondsOffset = doc["utc_offset_seconds"];
            if(secondsOffset != gmtOffset_sec) {
                gmtOffset_sec = secondsOffset;
                daylightOffset_sec = 0;
                configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
                
                // Save timezone to NVS for persistence
                Preferences prefs;
                prefs.begin("gh-config", false);
                prefs.putLong("gmt", gmtOffset_sec);
                prefs.putInt("dst", daylightOffset_sec);
                prefs.end();
                
                Serial.printf("[Time] TZ Auto-updated from location: %ld seconds (GMT%+d)\n", gmtOffset_sec, gmtOffset_sec/3600);
            }
            
            JsonObject current = doc["current"];
            JsonObject daily = doc["daily"];

            JsonDocument wUpdate;
            wUpdate["type"] = "weather";
            
            JsonObject wData = wUpdate["data"].to<JsonObject>();
            wData["valid"] = true;
            // Use actual temperature for greenhouse heating control
            float weatherTemp = current["temperature_2m"];
            wData["temp"] = weatherTemp;
            lastWeatherTemp = weatherTemp; // Store for routine triggers
            wData["humi"] = current["relative_humidity_2m"];
            wData["code"] = current["weather_code"];
            wData["wind"] = current["wind_speed_10m"];
            wData["is_day"] = current["is_day"] | 1;  // 1 = day, 0 = night
            wData["feels"] = current["apparent_temperature"];
            
            // Location/timezone info for display
            String tzName = doc["timezone"].as<String>();
            tzName.replace("_", " ");
            if(tzName.indexOf("/") > 0) {
                tzName = tzName.substring(tzName.indexOf("/") + 1);
            }
            wData["code_txt"] = tzName;
            
            if(daily.containsKey("temperature_2m_max")) {
                wData["max"] = daily["temperature_2m_max"][0];
                wData["min"] = daily["temperature_2m_min"][0];
            } else {
                wData["max"] = 0;
                wData["min"] = 0;
            }
            
            // Add hourly forecast (next 12 hours)
            JsonObject hourly = doc["hourly"];
            if(hourly.containsKey("time")) {
                JsonArray hArr = wData["hourly"].to<JsonArray>();
                JsonArray times = hourly["time"];
                JsonArray temps = hourly["temperature_2m"];
                JsonArray codes = hourly["weather_code"];
                JsonArray isDays = hourly["is_day"];
                
                // Find current hour index
                struct tm timeinfo;
                int currentHour = 0;
                if(getLocalTime(&timeinfo)) {
                    currentHour = timeinfo.tm_hour;
                }
                
                // Send next 12 hours starting from current hour
                for(int i = currentHour; i < min(currentHour + 12, (int)times.size()); i++) {
                    JsonObject h = hArr.add<JsonObject>();
                    String timeStr = times[i].as<String>();
                    // Extract hour from "2026-01-28T14:00" format
                    int hourIdx = timeStr.indexOf('T');
                    if(hourIdx > 0) {
                        h["time"] = timeStr.substring(hourIdx + 1, hourIdx + 6);
                    }
                    h["temp"] = temps[i];
                    h["code"] = codes[i];
                    h["is_day"] = isDays[i];
                }
            }

            String out;
            serializeJson(wUpdate, out);
            web.broadcastStatus(out);
            
            // Cache weather data to NVS for persistence across reboots
            Preferences weatherCache;
            weatherCache.begin("weather-cache", false);
            weatherCache.putString("json", out);
            
            // Store real Unix timestamp if we have valid time
            struct tm timeinfo;
            if(getLocalTime(&timeinfo)) {
                time_t now;
                time(&now);
                weatherCache.putULong("ts", (unsigned long)now);
                cachedWeatherTimestamp = (unsigned long)now;
            }
            weatherCache.end();
            cachedWeatherJson = out;
            weatherCacheStale = false;  // Cache is now fresh
            
            Serial.println("[Weather] Update sent and cached");
        } else {
            Serial.printf("[Weather] JSON Error: %s\n", error.c_str());
        }
    } else {
        Serial.printf("[Weather] HTTP Failed: %d\n", httpCode);
        web.broadcastStatus("{\"type\":\"weather\",\"data\":{\"valid\":false}}"
        );
    }
    

    http.end();
}

void listFiles()
{
    Serial.println(F("\n--- LittleFS Contents ---"));
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    if (!file) {
        Serial.println(F("! WARNING: LittleFS is EMPTY"));
    }
    while (file) {
        Serial.printf("%s (%d bytes)\n", file.name(), file.size());
        file = root.openNextFile();
    }
}

void startWiFi() {
    // Use new WiFi Provisioning System
    // Determine device type from config or use GENERIC
    int deviceTypeVal = 255;  // GENERIC
    Preferences prefs;
    prefs.begin("gh-config", true);
    if (prefs.isKey("deviceType")) {
        deviceTypeVal = prefs.getInt("deviceType", 255);
    }
    prefs.end();
    
    WiFiProvisioning::DeviceType devType = static_cast<WiFiProvisioning::DeviceType>(deviceTypeVal);
    WiFiProvisioning::begin(devType);
    
    Serial.println("[WIFI] WiFi Provisioning System Started");
}

void handleWiFiProvisioning() {
    // Call periodically from main loop to handle provisioning state machine
    WiFiProvisioning::update();
    
    // Once ready, update legacy global variables for backward compatibility
    if (WiFiProvisioning::isReady()) {
        isAPMode = false;
        if (WiFi.status() == WL_CONNECTED) {
            wifiReconnectAttempts = 0;
        }
        
        // Report network status to Pi every 10 seconds
        static unsigned long lastNetworkReport = 0;
        if (millis() - lastNetworkReport > 10000) {
            lastNetworkReport = millis();
            
            // Get Pi address and OTA settings from config
            Preferences prefs;
            prefs.begin("gh-config", true);
            String piIP = prefs.getString("pi", "");
            unsigned long otaCheckInterval = prefs.getULong("ota_interval", 3600000);  // Default: 1 hour
            prefs.end();
            
            if (piIP.length() > 0) {
                WiFiProvisioning::reportNetworkStatus(piIP.c_str());
                
                // Check for OTA updates at configured interval
                static unsigned long lastOTACheck = 0;
                if (millis() - lastOTACheck > otaCheckInterval) {
                    lastOTACheck = millis();
                    Serial.printf("[Main] Checking for OTA firmware updates (interval: %lu ms)...\n", otaCheckInterval);
                    WiFiProvisioning::checkAndDownloadOTA(piIP.c_str());
                }
            }
        }
    } else if (WiFiProvisioning::isAPMode()) {
        isAPMode = true;
    }
}

void loadConfig() {
    Preferences prefs;
    prefs.begin("gh-config", false); // Read/Write for possible migration

    // Migration: If NVS is empty but File exists, import it
    if (!prefs.isKey("ssid") && LittleFS.exists("/config.json")) {
        Serial.println("[CFG] Migrating config.json to NVS...");
        File f = LittleFS.open("/config.json", "r");
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f);
        f.close();
        
        if (!error) {
            if(doc.containsKey("ssid")) prefs.putString("ssid", doc["ssid"].as<String>());
            if(doc.containsKey("pass")) prefs.putString("pass", doc["pass"].as<String>());
            // Standardize 'pi' vs 'piIp'
            if(doc.containsKey("pi")) prefs.putString("pi", doc["pi"].as<String>());
            else if(doc.containsKey("piIp")) prefs.putString("pi", doc["piIp"].as<String>());

            if(doc.containsKey("proxy")) prefs.putBool("proxy", doc["proxy"]);
            if(doc.containsKey("ntp")) prefs.putString("ntp", doc["ntp"].as<String>());
            if(doc.containsKey("gmt")) prefs.putLong("gmt", doc["gmt"]);
            if(doc.containsKey("dst")) prefs.putInt("dst", doc["dst"]);
            if(doc.containsKey("lat")) prefs.putString("lat", doc["lat"].as<String>());
            if(doc.containsKey("lon")) prefs.putString("lon", doc["lon"].as<String>());
        }
    }
    
    // Load to Globals - use char array methods
    String temp = prefs.getString("ssid", "");
    strncpy(savedSSID, temp.c_str(), sizeof(savedSSID) - 1);
    temp = prefs.getString("city", "");
    strncpy(cfgCity, temp.c_str(), sizeof(cfgCity) - 1);
    temp = prefs.getString("region", "");
    strncpy(cfgRegion, temp.c_str(), sizeof(cfgRegion) - 1);
    temp = prefs.getString("pi", "100.92.151.67"); // Default to verified Pi IP
    strncpy(piIp, temp.c_str(), sizeof(piIp) - 1);
    useProxy = prefs.getBool("proxy", false);
    temp = prefs.getString("ntp", "pool.ntp.org");
    strncpy(ntpServer, temp.c_str(), sizeof(ntpServer) - 1);
    gmtOffset_sec = prefs.getLong("gmt", 0);
    daylightOffset_sec = prefs.getInt("dst", 0);
    temp = prefs.getString("lat", "");
    strncpy(cfgLat, temp.c_str(), sizeof(cfgLat) - 1);
    temp = prefs.getString("lon", "");
    strncpy(cfgLon, temp.c_str(), sizeof(cfgLon) - 1);
    temp = prefs.getString("unit", "c");
    strncpy(cfgUnit, temp.c_str(), sizeof(cfgUnit) - 1);
    cfgAmpThreshold = prefs.getFloat("ampThresh", 0.25f);
    
    Serial.println(F("[CFG] Loaded values from NVS:"));
    Serial.printf(" - SSID: %s\n", savedSSID);
    Serial.printf(" - Lat: %s, Lon: %s\n", cfgLat, cfgLon);
    Serial.printf(" - Unit: %s\n", cfgUnit);
    Serial.printf(" - Amp Threshold: %.2fA\n", cfgAmpThreshold);
    
    prefs.end();
    
    // Load cached weather data
    Preferences weatherCache;
    weatherCache.begin("weather-cache", true);  // Read-only
    cachedWeatherJson = weatherCache.getString("json", "");
    cachedWeatherTimestamp = weatherCache.getULong("ts", 0);
    weatherCache.end();
    
    if(cachedWeatherJson.length() > 0) {
        // Check if cache is still valid (less than 30 minutes old using real time)
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)) {
            time_t now;
            time(&now);
            unsigned long ageSeconds = (unsigned long)now - cachedWeatherTimestamp;
            Serial.printf("[CFG] Loaded cached weather (age: %lu seconds)\\n", ageSeconds);
            
            // If cache is fresh enough, use its timestamp to set lastWeatherUpdate
            // so we don't immediately refresh
            if(ageSeconds < (WEATHER_UPDATE_INTERVAL / 1000)) {
                // Cache is still valid, delay next refresh accordingly
                lastWeatherUpdate = millis() - (ageSeconds * 1000);
                weatherCacheStale = false;
                Serial.println("[CFG] Weather cache is fresh, will use threshold timing");
            } else {
                weatherCacheStale = true;
                Serial.println("[CFG] Weather cache is stale, will refresh soon");
            }
        } else {
            Serial.println("[CFG] Loaded cached weather (time not synced yet)");
        }
    }
}

// Helper to broadcast device sync to all connected clients
void broadcastDeviceSync(AsyncWebSocketClient *client) {
    JsonDocument sync;
    sync["type"] = "sync";
    JsonArray devArr = sync["devices"].to<JsonArray>();
    deviceMgr.toJson(devArr);
    
    String syncOut;
    serializeJson(sync, syncOut);
    web.broadcastStatus(syncOut);
}

void handleSocketConnect(AsyncWebSocketClient *client)
{
    // DON'T send data synchronously here - it can block the AsyncTCP task
    // Instead, set flag for SyncTask to handle it safely
    if(cachedWeatherJson.length() > 0) {
        pendingCacheBroadcast = true;
        Serial.println("[WS] New client - will send cached weather via SyncTask");
    }
    
    // Schedule weather refresh in 5 seconds (non-blocking)
    // Only if cache is stale and we're connected to WiFi
    if(weatherCacheStale && !isAPMode && WiFi.status() == WL_CONNECTED) {
        pendingWeatherRefresh = millis() + 5000;
        Serial.println("[WS] Scheduled weather refresh in 5 seconds (cache stale)");
    }
}

void handleSocketData(AsyncWebSocketClient *client, uint8_t *data)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, (char *)data);
    if (error) return;

    if (doc["type"] == "config_update")
    {
        Preferences prefs;
        prefs.begin("gh-config", false); // Read/Write

        // 1. Preserve Password if empty
        String newPass = doc["pass"].as<String>();
        
        if (newPass.length() > 0) {
            prefs.putString("pass", newPass);
        } else {
             // Keep existing pass in NVS (do nothing) or verify?
             // If we were serializing to JSON, we needed to read old one back to write it.
             // With NVS, if we don't overwrite it, it stays!
             // So we don't need to read oldPass unless we want to echo it back (rebooting anyway).
        }

        // 2. Save New Config to NVS
        if(doc.containsKey("ssid")) prefs.putString("ssid", doc["ssid"].as<String>());
        // UI sends 'piIp' usually? let's save as 'pi' to standardize
        if(doc.containsKey("piIp")) prefs.putString("pi", doc["piIp"].as<String>());
        else if(doc.containsKey("pi")) prefs.putString("pi", doc["pi"].as<String>());

        if(doc.containsKey("proxy")) prefs.putBool("proxy", doc["proxy"]);
        if(doc.containsKey("ntp")) prefs.putString("ntp", doc["ntp"].as<String>());
        if(doc.containsKey("gmt")) prefs.putLong("gmt", doc["gmt"]);
        if(doc.containsKey("dst")) prefs.putInt("dst", doc["dst"]);
        if(doc.containsKey("ampThresh")) {
            float newThresh = doc["ampThresh"].as<float>();
            prefs.putFloat("ampThresh", newThresh);
            cfgAmpThreshold = newThresh;
            // Apply threshold immediately to relay and routine systems
            relays.setAmpThreshold(newThresh);
            routineMgr.setAmpThreshold(newThresh);
            Serial.printf("[CFG] Amp threshold set to: %.2fA\n", newThresh);
        }
        
        prefs.end();
        
        Serial.println("[CFG] Configuration saved to NVS. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
    }
    else if (doc["type"] == "time_set_manual")
    {
        // One-time manual set (useful for offline mode)
        // Payload: "epoch": 1672345678
        unsigned long epoch = doc["epoch"];
        struct timeval tv;
        tv.tv_sec = epoch;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        Serial.println("[TIME] Manual time set received.");
    }
    else if (doc["type"] == "config_location")
    {
        String lat = doc["lat"].as<String>();
        String lon = doc["lon"].as<String>();
        strncpy(cfgLat, lat.c_str(), sizeof(cfgLat) - 1);
        strncpy(cfgLon, lon.c_str(), sizeof(cfgLon) - 1);
        
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.putString("lat", cfgLat);
        prefs.putString("lon", cfgLon);
        prefs.end();
        
        Serial.printf("[LOC] Updated Manual (NVS): %s, %s\n", cfgLat, cfgLon);
        
        // Force Weather Update
        lastWeatherUpdate = 0; 
    }
    else if (doc["type"] == "config_unit")
    {
        String unit = doc["unit"].as<String>();
        strncpy(cfgUnit, unit.c_str(), sizeof(cfgUnit) - 1);
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.putString("unit", cfgUnit);
        prefs.end();
        Serial.printf("[CFG] Unit set to (NVS): %s\n", cfgUnit);
    }
    else if (doc["type"] == "scan_wifi")
    {
        // Don't scan here! It blocks the socket thread.
        // Set flag for the main Network Task to handle.
        scanRequested = true;
    }
    else if (doc["type"] == "move_device")
    {
        String id = doc["id"];
        // Check if this is a mobile or desktop update
        if (doc["x_mobile"].is<int>() && doc["y_mobile"].is<int>()) {
            // Mobile position update
            int x_mobile = doc["x_mobile"];
            int y_mobile = doc["y_mobile"];
            deviceMgr.updateMobilePosition(id, x_mobile, y_mobile);
            Serial.printf("[DEV] Moved device %s to mobile pos (%d, %d)\n", id.c_str(), x_mobile, y_mobile);
        } else {
            // Desktop position update
            int x = doc["x"];
            int y = doc["y"];
            deviceMgr.updatePosition(id, x, y);
            Serial.printf("[DEV] Moved device %s to desktop pos (%d, %d)\n", id.c_str(), x, y);
        }
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "create_device")
    {
        String type = doc["type_id"]; // e.g., 'heater', 'fan'
        int x = doc["x"];
        int y = doc["y"];
        deviceMgr.createDevice(type, x, y);
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "update_device")
    {
        String id = doc["id"];
        String name = doc["name"];
        int ch = doc["ch"] | 0;
        deviceMgr.updateDetails(id, name, ch);
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "update_device_physical")
    {
        String id = doc["id"];
        String name = doc["name"];
        int rotation = doc["rotation"] | -999;  // Use -999 as sentinel value
        int rotation_mobile = doc["rotation_mobile"] | -999;
        int ch = doc["channel"] | 0;
        PhysicalDeviceType physType = static_cast<PhysicalDeviceType>(doc["phys_type"] | 0);
        String physAddr = doc["phys_addr"].as<String>();
        int physPin = doc["phys_pin"] | -1;
        bool enabled = doc["enabled"] | true;  // Default to enabled if not specified
        
        deviceMgr.updatePhysicalDevice(id, name, ch, physType, physAddr, physPin);
        
        // Update rotation for desktop if provided
        if(rotation != -999) {
            deviceMgr.updateRotation(id, rotation);
        }
        
        // Update rotation for mobile if provided
        if(rotation_mobile != -999) {
            deviceMgr.updateRotationMobile(id, rotation_mobile);
        }
        
        // If disabling, turn OFF the device first
        if (!enabled) {
            int channel = deviceMgr.setState(id, false);
            if(channel > 0 && channel <= 15) {
                relays.pulseRelay(channel); // Turn off relay
            }
        }
        
        deviceMgr.setEnabled(id, enabled);  // Update enabled state
        Serial.printf("[DEV] Updated physical device: %s, Rotation: %d/%d, Type: %d, Pin: %d, Enabled: %d\n", 
                     id.c_str(), rotation, rotation_mobile, static_cast<int>(physType), physPin, enabled);
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "delete_device")
    {
        String id = doc["id"];
        deviceMgr.deleteDevice(id);
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "clear_all_devices")
    {
        // SAFETY: Require password confirmation to prevent accidental data loss
        String confirm = doc["confirm"] | "";
        if (confirm != "DELETE_ALL_FOREVER") {
            Serial.println("[WS] ⚠️ Clear all devices REJECTED - missing confirmation password");
            client->text("{\"type\":\"error\",\"message\":\"Confirmation required\"}");
            return;
        }
        Serial.println("[WS] ⚠️⚠️⚠️ Clear all devices CONFIRMED with password");
        deviceMgr.createDefaultLayout();
        Serial.println("[WS] All devices cleared");
        // Broadcast updated device list
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "force_save_layout")
    {
        deviceMgr.saveLayout();
        Serial.println("[DEV] Forced layout save from client");
        // Also broadcast sync after save
        broadcastDeviceSync(client);
    }
    else if (doc["type"] == "set_state")
    {
        String id = doc["id"];
        bool state = doc["state"];
        int ch = deviceMgr.setState(id, state);
        if(ch > 0 && ch <= 15) {
             // Only pulse if state changed? Actually pulseRelay is a toggle hardware-wise for latching relays? 
             // Or is it a momentary pulse to a latching relay controller?
             // Assuming pulseRelay toggles the state, we need to be careful if we are "Setting" state.
             // If the hardware state is unknown, this might desync.
             // For now, assuming the user knows what they are doing with the button.
            relays.pulseRelay(ch); 
            Serial.printf("[RELAY] Pulsed Channel %d (Set: %d)\n", ch, state);
        }
    }
    else if (doc["type"] == "set_enabled")
    {
        String id = doc["id"];
        bool enabled = doc["enabled"];
        deviceMgr.setEnabled(id, enabled);
    }
    else if (doc["type"] == "toggle")
    {
        // Simple Toggle Logic (requires mapping IDs to relays)
        String id = doc["id"];
        int ch = deviceMgr.toggle(id);
        if(ch > 0 && ch <= 15) {
            float deltaAmps = relays.pulseRelay(ch);
            Serial.printf("[RELAY] Pulsed Channel %d\n", ch);
            
            // Get device info for alert
            const auto* device = deviceMgr.getDevice(id);
            if(device) {
                bool newState = device->active;
                float measuredAmps = relays.getDeviceAmps(ch);
                bool confirmed = newState ? (measuredAmps > 0.1f) : (measuredAmps < 0.1f);
                
                // Send relay state change alert
                alertMgr.alertRelayChange(device->name, ch, newState, measuredAmps, confirmed);
            }
            
            // Immediately broadcast updated state
            JsonDocument syncDoc;
            syncDoc["type"] = "sync";
            JsonArray devArr = syncDoc["devices"].to<JsonArray>();
            deviceMgr.toJson(devArr);
            JsonObject cfg = syncDoc["config"].to<JsonObject>();
            cfg["unit"] = cfgUnit;
            
            // Send saved location info
            Preferences prefs;
            prefs.begin("gh-config", true);
            String city = prefs.getString("city", "");
            String region = prefs.getString("region", "");
            prefs.end();
            if(city.length() > 0) cfg["city"] = city;
            if(region.length() > 0) cfg["region"] = region;
            String syncOut;
            serializeJson(syncDoc, syncOut);
            web.broadcastStatus(syncOut);
        }
    }
    else if (doc["type"] == "set_timezone")
    {
        gmtOffset_sec = doc["gmt"] | 0;
        daylightOffset_sec = doc["dst"] | 0;
        
        // Save to NVS
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.putInt("gmt", gmtOffset_sec);
        prefs.putInt("dst", daylightOffset_sec);
        prefs.end();
        
        // Apply timezone
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        Serial.printf("[TIME] Timezone updated: GMT%+d DST%+d\n", gmtOffset_sec/3600, daylightOffset_sec/3600);
    }
    else if (doc["type"] == "set_location")
    {
        String lat = doc["lat"].as<String>();
        String lon = doc["lon"].as<String>();
        strncpy(cfgLat, lat.c_str(), sizeof(cfgLat) - 1);
        strncpy(cfgLon, lon.c_str(), sizeof(cfgLon) - 1);
        
        // Save to NVS
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.putString("lat", cfgLat);
        prefs.putString("lon", cfgLon);
        prefs.end();
        
        Serial.printf("[LOC] Location updated: %s, %s\n", cfgLat, cfgLon);
        
        // Trigger weather update
        lastWeatherUpdate = 0;
    }
    else if (doc["type"] == "update_location_names")
    {
        String city = doc["city"].as<String>();
        String region = doc["region"].as<String>();
        strncpy(cfgCity, city.c_str(), sizeof(cfgCity) - 1);
        strncpy(cfgRegion, region.c_str(), sizeof(cfgRegion) - 1);
        
        // Save to NVS for persistence
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.putString("city", cfgCity);
        prefs.putString("region", cfgRegion);
        prefs.end();
        
        Serial.printf("[LOC] City/Region updated: %s, %s\n", cfgCity, cfgRegion);
    }
    else if (doc["type"] == "set_time" || doc["type"] == "time_sync")
    {
        // Time sync from browser or Pi proxy
        unsigned long unixTime = doc["unix"] | 0;
        int newGmt = doc["gmt"] | gmtOffset_sec;  // Keep existing if not provided
        int newDst = doc["dst"] | daylightOffset_sec;
        
        // Only update timezone if changed
        if (newGmt != gmtOffset_sec || newDst != daylightOffset_sec) {
            gmtOffset_sec = newGmt;
            daylightOffset_sec = newDst;
            
            // Save timezone to NVS
            Preferences prefs;
            prefs.begin("gh-config", false);
            prefs.putInt("gmt", gmtOffset_sec);
            prefs.putInt("dst", daylightOffset_sec);
            prefs.end();
            
            // Reconfigure timezone
            configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
        }
        
        // Set system time
        struct timeval tv;
        tv.tv_sec = unixTime;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);
        
        // Log only occasionally to reduce spam
        static unsigned long lastTimeLog = 0;
        if (millis() - lastTimeLog > 60000) {
            Serial.printf("[TIME] Time synced: %lu (GMT+%d)\n", unixTime, gmtOffset_sec / 3600);
            lastTimeLog = millis();
        }
    }
    else if (doc["type"] == "get_sync" || doc["type"] == "sync")
    {
        // Routines page or restore tool requesting device list
        Serial.println("[SYNC] sync requested - sending device list...");
        
        JsonDocument sync;
        sync["type"] = "sync";
        JsonArray devArr = sync["devices"].to<JsonArray>();
        deviceMgr.toJson(devArr);
        
        String syncOut;
        serializeJson(sync, syncOut);
        client->text(syncOut);
    }
    else if (doc["type"] == "reboot")
    {
        Serial.println("[SYS] Reboot requested via WebSocket");
        client->text("{\"status\":\"rebooting\"}");
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
    }
    else if (doc["type"] == "get_heap")
    {
        // Return heap/memory info for setup page
        JsonDocument heap;
        heap["type"] = "heap";
        heap["free"] = ESP.getFreeHeap();
        heap["min"] = ESP.getMinFreeHeap();
        heap["total"] = ESP.getHeapSize();
        String out;
        serializeJson(heap, out);
        client->text(out);
    }
    else if (doc["type"] == "factory_reset")
    {
        Serial.println("[SYS] ⚠️ FACTORY RESET requested!");
        // Clear NVS preferences
        Preferences prefs;
        prefs.begin("gh-config", false);
        prefs.clear();
        prefs.end();
        // Clear device layout
        deviceMgr.createDefaultLayout();
        // Clear routines - delete the file and reinitialize
        if(LittleFS.exists("/routines.json")) {
            LittleFS.remove("/routines.json");
        }
        // Respond and reboot
        client->text("{\"status\":\"factory_reset_complete\"}");
        Serial.println("[SYS] Factory reset complete. Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
    }
    else if (doc["type"] == "refresh_weather")
    {
        Serial.println("[Weather] Manual refresh requested");
        fetchWeather();
        lastWeatherUpdate = millis();
    }
    // Routine Management
    else if (doc["type"] == "create_routine")
    {
        String name = doc["name"].as<String>();
        RoutineTriggerType triggerType = static_cast<RoutineTriggerType>(doc["trigger_type"] | 0);
        String id = routineMgr.createRoutine(name, triggerType);
        
        JsonDocument response;
        response["type"] = "routine_created";
        response["id"] = id;
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ROUTINE] Created: %s (%s)\n", name.c_str(), id.c_str());
    }
    else if (doc["type"] == "delete_routine")
    {
        String id = doc["id"].as<String>();
        routineMgr.deleteRoutine(id);
        Serial.printf("[ROUTINE] Deleted: %s\n", id.c_str());
    }
    else if (doc["type"] == "update_routine")
    {
        String id = doc["id"].as<String>();
        String name = doc["name"].as<String>();
        RoutineTriggerType triggerType = static_cast<RoutineTriggerType>(doc["trigger_type"] | 0);
        
        // New auto-reverse parameters
        bool autoReverse = doc["auto_reverse"] | true;
        float hysteresis = doc["hysteresis"] | 2.0f;
        int maxRunSeconds = doc["max_run_seconds"] | 0;
        
        routineMgr.updateRoutine(id, name, triggerType,
            doc["temp_min"] | 15.0f,
            doc["temp_max"] | 30.0f,
            doc["timer_seconds"] | 0,
            doc["schedule"].as<String>(),
            autoReverse,
            hysteresis,
            maxRunSeconds);
        Serial.printf("[ROUTINE] Updated: %s (autoReverse=%d, hysteresis=%.1f, maxRun=%ds)\n", 
            id.c_str(), autoReverse, hysteresis, maxRunSeconds);
    }
    else if (doc["type"] == "set_routine_enabled")
    {
        String id = doc["id"].as<String>();
        bool enabled = doc["enabled"] | false;
        routineMgr.setEnabled(id, enabled);
        Serial.printf("[ROUTINE] %s: %s\n", id.c_str(), enabled ? "enabled" : "disabled");
    }
    else if (doc["type"] == "add_routine_step")
    {
        String id = doc["id"].as<String>();
        String type = doc["step_type"].as<String>();
        ActionType action = static_cast<ActionType>(doc["action"] | 0);
        int waitSeconds = doc["wait_seconds"] | 0;
        
        std::vector<String> deviceIds;
        JsonArray devs = doc["device_ids"].as<JsonArray>();
        for(JsonVariant v : devs) {
            deviceIds.push_back(v.as<String>());
        }
        
        routineMgr.addStep(id, type, deviceIds, action, waitSeconds);
        
        // Update device sequence and timers if provided
        if(doc.containsKey("device_sequence") || doc.containsKey("device_timers") || doc.containsKey("execution_mode")) {
            auto routine = routineMgr.getRoutine(id);
            if(routine && !routine->steps.empty()) {
                auto &lastStep = routine->steps.back();
                
                // Parse device sequence
                if(doc.containsKey("device_sequence")) {
                    JsonArray seqArr = doc["device_sequence"].as<JsonArray>();
                    lastStep.deviceSequence.clear();
                    for(JsonVariant v : seqArr) {
                        lastStep.deviceSequence.push_back(v.as<String>());
                    }
                }
                
                // Parse device timers
                if(doc.containsKey("device_timers")) {
                    JsonObject timersObj = doc["device_timers"].as<JsonObject>();
                    lastStep.deviceTimers.clear();
                    for(JsonPair kv : timersObj) {
                        lastStep.deviceTimers[kv.key().c_str()] = kv.value().as<float>();
                    }
                }
                
                // Parse execution mode
                if(doc.containsKey("execution_mode")) {
                    lastStep.executionMode = doc["execution_mode"].as<String>();
                }
            }
        }
        
        Serial.printf("[ROUTINE] Added step to %s\n", id.c_str());
    }
    else if (doc["type"] == "clear_routine_steps")
    {
        String id = doc["id"].as<String>();
        routineMgr.clearSteps(id);
        Serial.printf("[ROUTINE] Cleared steps: %s\n", id.c_str());
    }
    else if (doc["type"] == "execute_routine")
    {
        String id = doc["id"].as<String>();
        bool started = false;
        
        // Check if manual action override is provided (for manual routines)
        if(doc.containsKey("manual_action")) {
            String action = doc["manual_action"].as<String>();
            ActionType manualAction = (action == "ON") ? ACTION_ON : ACTION_OFF;
            started = routineMgr.startRoutine(id, manualAction);
            Serial.printf("[ROUTINE] Manual execution with action: %s\n", action.c_str());
        } else {
            started = routineMgr.startRoutine(id);
        }
        
        JsonDocument response;
        response["type"] = "routine_started";
        response["id"] = id;
        response["success"] = started;
        String out;
        serializeJson(response, out);
        client->text(out);
        
        Serial.printf("[ROUTINE] %s: %s\n", started ? "Started" : "Failed to start", id.c_str());
    }
    else if (doc["type"] == "stop_routine")
    {
        String id = doc["id"].as<String>();
        bool stopped = routineMgr.stopRoutine(id);
        Serial.printf("[ROUTINE] Stop %s: %s\n", id.c_str(), stopped ? "success" : "failed");
    }
    else if (doc["type"] == "sync_routines")
    {
        JsonDocument response;
        response["type"] = "routines_sync";
        JsonArray arr = response["routines"].to<JsonArray>();
        routineMgr.toJson(arr);
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.println("[ROUTINE] Synced routines to client");
    }
    // === CURRENT SENSOR MANAGEMENT ===
    else if (doc["type"] == "calibrate_current_sensor")
    {
        Serial.println("[Current] Calibration requested via WebSocket");
        Serial.println("[Current] ⚠️ Ensure no loads are active for accurate calibration!");
        currentSensor.calibrate();
        
        JsonDocument response;
        response["type"] = "current_calibrated";
        response["success"] = currentSensor.isCalibrated();
        String out;
        serializeJson(response, out);
        client->text(out);
    }
    else if (doc["type"] == "get_current_data")
    {
        // Return comprehensive current data
        JsonDocument response;
        response["type"] = "current_data";
        response["total_amps"] = relays.getTotalAmps();
        response["calibrated"] = currentSensor.isCalibrated();
        response["raw_adc"] = currentSensor.getRawADC();
        response["voltage"] = currentSensor.getVoltage();
        
        JsonArray devices = response["devices"].to<JsonArray>();
        for(const auto &d : deviceMgr.devices) {
            if(d.hardwareChannel > 0 && d.hardwareChannel <= 15) {
                JsonObject dev = devices.add<JsonObject>();
                dev["id"] = d.id;
                dev["name"] = d.name;
                dev["ch"] = d.hardwareChannel;
                dev["amps"] = relays.getDeviceAmps(d.hardwareChannel);
                dev["on"] = relays.getDeviceState(d.hardwareChannel);
                dev["healthy"] = relays.isDeviceHealthy(d.hardwareChannel);
            }
        }
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.println("[Current] Sent current data to client");
    }
    // === ALERT SYSTEM MANAGEMENT ===
    else if (doc["type"] == "get_alerts_config")
    {
        // Return alert configuration
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        
        // WhatsApp contacts array at root level
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        
        // Telegram bots array at root level
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        
        // Alerts object at root level
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.println("[ALERT] Sent alerts config to client");
    }
    else if (doc["type"] == "update_alerts_config")
    {
        // Update alert configuration from frontend
        if (doc.containsKey("enabled")) {
            alertMgr.setEnabled(doc["enabled"].as<bool>());
        }
        if (doc.containsKey("contacts")) {
            // Contacts updated via separate handlers
        }
        
        JsonDocument response;
        response["type"] = "alerts_config_updated";
        response["success"] = true;
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.println("[ALERT] Updated alerts config");
    }
    else if (doc["type"] == "add_alert_contact")
    {
        String phone = doc["phone"].as<String>();
        String apiKey = doc["apiKey"].as<String>();
        String name = doc["name"].as<String>();
        int minPriority = doc["minPriority"] | 0;
        
        bool success = alertMgr.addContact(phone, apiKey, name, static_cast<AlertPriority>(minPriority));
        
        // Send back full config
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Contact %s: %s\n", success ? "added" : "failed", name.c_str());
    }
    else if (doc["type"] == "remove_alert_contact")
    {
        String phone = doc["phone"].as<String>();
        bool success = alertMgr.removeContact(phone);
        
        // Send back full config
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Contact removed: %s\n", phone.c_str());
    }
    else if (doc["type"] == "add_telegram_bot")
    {
        String botToken = doc["botToken"].as<String>();
        String chatId = doc["chatId"].as<String>();
        String name = doc["name"].as<String>();
        int minPriority = doc["minPriority"] | 0;
        
        bool success = alertMgr.addTelegramBot(botToken, chatId, name, static_cast<AlertPriority>(minPriority));
        
        // Send back full config
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Telegram bot %s: %s\n", success ? "added" : "failed", name.c_str());
    }
    else if (doc["type"] == "remove_telegram_bot")
    {
        String chatId = doc["chatId"].as<String>();
        bool success = alertMgr.removeTelegramBot(chatId);
        
        // Send back full config
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Telegram bot removed: %s\n", chatId.c_str());
    }
    else if (doc["type"] == "test_alert")
    {
        bool success = alertMgr.sendTestAlert();
        
        JsonDocument response;
        response["type"] = "test_alert_result";
        response["success"] = success;
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Test alert: %s\n", success ? "sent" : "failed");
    }
    else if (doc["type"] == "update_alert_setting")
    {
        int alertType = doc["alertType"] | 0;
        
        // Get config either from nested object or directly
        bool enabled = true;
        uint16_t cooldown = 30;
        uint8_t maxPerHour = 5;
        float threshold = 0.0f;
        String triggerRoutine = "";
        
        if (doc.containsKey("config")) {
            JsonObject cfg = doc["config"];
            enabled = cfg["enabled"] | true;
            cooldown = cfg["cooldown"] | 30;
            maxPerHour = cfg["maxPerHour"] | 5;
            threshold = cfg["threshold"] | 0.0f;
            triggerRoutine = cfg["triggerRoutine"].as<String>();
        } else {
            enabled = doc["enabled"] | true;
            cooldown = doc["cooldown"] | 30;
            threshold = doc["threshold"] | 0.0f;
            triggerRoutine = doc["triggerRoutine"].as<String>();
        }
        
        alertMgr.setAlertConfig(static_cast<AlertType>(alertType), enabled, cooldown, threshold, triggerRoutine);
        
        // Send back updated config
        JsonDocument response;
        response["type"] = "alerts_config";
        response["enabled"] = alertMgr.isEnabled();
        JsonArray contactsArr = response["contacts"].to<JsonArray>();
        alertMgr.getContactsJson(contactsArr);
        JsonArray telegramArr = response["telegram"].to<JsonArray>();
        alertMgr.getTelegramJson(telegramArr);
        JsonObject alertsObj = response["alerts"].to<JsonObject>();
        alertMgr.getAlertsJson(alertsObj);
        
        String out;
        serializeJson(response, out);
        client->text(out);
        Serial.printf("[ALERT] Updated alert type %d config\n", alertType);
    }
    else if (doc["type"] == "proxy_status")
    {
        // Pi proxy notifies ESP32 it's connected
        bool esp32Connected = doc["esp32Connected"] | false;
        if (esp32Connected) {
            proxyConnected = true;
            lastProxyPing = millis();  // Update ping timer
            Serial.println("[Proxy] ✓ Connected to Pi proxy! Weather checking PAUSED.");
            // Broadcast status to UI
            web.broadcastStatus(F("{\"type\":\"proxy_status\",\"connected\":true}"));
        }
    }
}

void setup()
{
    Serial.begin(115200);

    // 1. Initialize Filesystems
    if (!LittleFS.begin(true)) {
        Serial.println(F("! LittleFS Mount Failed"));
    } else {
        listFiles();
    }

    // Start a timer to check proxy connection status
    xTaskCreatePinnedToCore([](void*) {
        for(;;) {
            if (proxyConnected && (millis() - lastProxyPing > PROXY_TIMEOUT_MS)) {
                proxyConnected = false;
                Serial.println("[Proxy] Lost connection to Pi proxy (timeout)");
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }, "ProxyConnCheck", 2048, nullptr, 1, nullptr, 1);
    
    // Initialize SD card (non-blocking if fails)
    sdCard.begin();

    // 2. Initialize NVS Flash
    esp_err_t nvs_err = nvs_flash_init();
    
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        Serial.println(F("[NVS] Recovery needed..."));
        nvs_err = nvs_flash_init_partition("nvs");
        if (nvs_err != ESP_OK) {
            nvs_err = nvs_flash_init();
        }
    }
    
    if (nvs_err != ESP_OK) {
        Serial.printf("[NVS] Init error: %d\n", nvs_err);
    }

    // 3. Hardware and Network
    Serial.println(F("[BOOT] Starting..."));
    Serial.printf("[BOOT] Free Heap: %d bytes\n", ESP.getFreeHeap());
    deviceMgr.begin();
    Serial.printf("[BOOT] Devices: %d\n", deviceMgr.devices.size());
    routineMgr.init();
    
    loadConfig(); // Load Saved Config!
    
    // Apply configurable amp threshold to all systems
    relays.setAmpThreshold(cfgAmpThreshold);
    routineMgr.setAmpThreshold(cfgAmpThreshold);
    
    // Initialize current sensor BEFORE relays
    currentSensor.begin(CURRENT_SENSOR_PIN);
    
    // Initialize DS18B20 temperature sensors
    tempSensors.begin();
    tempSensors.setResolution(12);
    tempSensors.setWaitForConversion(false);
    sensorCount = tempSensors.getDeviceCount();
    Serial.printf("[BOOT] DS18B20 sensors: %d\n", sensorCount);
    
    // Store sensor addresses
    for(int i = 0; i < min(sensorCount, 6); i++) {
        tempSensors.getAddress(sensorAddresses[i], i);
    }
    
    relays.begin();
    relays.attachCurrentSensor(&currentSensor);  // Enable delta-based current monitoring
    
    // Restore saved relay states with staggered delay
    for(const auto& d : deviceMgr.devices) {
        if(d.active && d.enabled && d.hardwareChannel > 0 && d.hardwareChannel <= 15) {
            relays.pulseRelay(d.hardwareChannel);
            relays.syncDeviceState(d.hardwareChannel, true);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    
    startWiFi();
    web.begin(sdCard);
    
    // Initialize Alert Manager (self-contained, uses "greenhouse" NVS namespace like DeviceManager/RoutineManager)
    alertMgr.begin();
    
    // Connect AlertManager to DeviceManager for floorplan generation
    alertMgr.setDeviceManager(&deviceMgr);
    
    // Send reboot alert NOW (after WiFi connected) with IP address
    if (WiFi.status() == WL_CONNECTED) {
        alertMgr.sendRebootAlert(WiFi.localIP().toString());
    }
    
    // Set routine trigger callback for alerts
    alertMgr.setRoutineCallback([](const String& routineName) {
        Serial.printf("[ALERT] Triggering routine: %s\n", routineName.c_str());
        routineMgr.startRoutineByName(routineName);
    });
    
    // Set routine completion callback for device confirmation results
    routineMgr.setFailureCallback([](const String& routineName, const std::vector<DeviceConfirmResult>& results) {
        // Convert DeviceConfirmResult to tuple format for alert
        std::vector<std::tuple<String, String, int, bool, float, bool>> alertResults;
        alertResults.reserve(results.size());
        for (const auto& r : results) {
            alertResults.emplace_back(r.deviceId, r.deviceName, r.channel, r.targetState, r.deltaAmps, r.confirmed);
        }
        alertMgr.alertRoutineDeviceFailures(routineName, alertResults);
    });
    
    // OTA & Anti-Brick
    OTAManager::begin(web.getServer());
    OTAManager::confirmUpdate();

    // Use loaded config for NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Register device with Pi server after WiFi and config are ready
    if (WiFi.status() == WL_CONNECTED && strlen(piIp) > 5) {
        registerDeviceWithPi();
    }

    /*
     * ====== FREERTOS TASK ARCHITECTURE ======
     * Priority order (higher = more important):
     *   1. Current Sensor: ON-DEMAND only (during relay toggle) - HIGHEST
     *   2. Relay Triggers: Priority 3 on Core 0 - SECOND
     *   3. Temperature Sensors: Priority 2 on Core 0 - THIRD (5s interval)
     *   4. UI Sync/Telemetry: Priority 1 on Core 0 - FOURTH (2s interval)  
     *   5. Network/Web/Alerts: Priority 1 on Core 1 - BACKGROUND
     *
     * Core 0: Hardware tasks (relays, sensors, sync)
     * Core 1: Network tasks (WiFi, HTTP, WebSocket, alerts)
     */

    // ===== CORE 0: TEMPERATURE SENSOR TASK (Priority 2) =====
    // Reads 6 DS18B20 sensors every 5 seconds
    xTaskCreatePinnedToCore([](void *p) {
        TickType_t xLastWakeTime = xTaskGetTickCount();
        constexpr TickType_t xFrequency = pdMS_TO_TICKS(5000); // 5s interval
        
        for(;;) {
            yield();
            
            if(sensorCount > 0) {
                // Request temperature conversion (async)
                tempSensors.requestTemperatures();
                
                // Wait for conversion (750ms for 12-bit)
                vTaskDelay(pdMS_TO_TICKS(800));
                
                float tempSum = 0.0f;
                int validReadings = 0;
                
                // Read all sensors and update DeviceManager
                for(int i = 0; i < min(sensorCount, 6); i++) {
                    float temp = tempSensors.getTempC(sensorAddresses[i]);
                    
                    // Validate reading (-127 = disconnected, 85 = power-on reset)
                    if(temp > -50.0f && temp < 85.0f) {
                        tempSum += temp;
                        validReadings++;
                        
                        // Build address string to match with DeviceManager
                        char addrStr[17];
                        for(int j = 0; j < 8; j++) sprintf(addrStr + j*2, "%02X", sensorAddresses[i][j]);
                        
                        // Update matching device in DeviceManager
                        for(auto &d : deviceMgr.devices) {
                            if(d.physicalType == PHYSICAL_DS18B20 && 
                               (d.physicalAddress == String(addrStr) || d.hardwareChannel == (i + 1))) {
                                d.lastValue = temp;
                                d.active = true;
                            }
                        }
                    }
                    // Yield between sensor reads to allow other tasks
                    taskYIELD();
                }
                
                // Update global average temperature
                if(validReadings > 0) {
                    currentTemperature = tempSum / validReadings;
                }
                
                // Log periodically
                static unsigned long lastTempLog = 0;
                if(millis() - lastTempLog > 30000) {
                    Serial.printf("[TEMP] Read %d sensors, avg=%.2f°C\n", validReadings, currentTemperature);
                    lastTempLog = millis();
                }
            }
            
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    }, "TempTask", 4096, NULL, 2, NULL, 0);  // Priority 2, Core 0

    // ===== CORE 0: CURRENT SENSOR TASK (Priority 1) =====
    // Updates amp reading cache every 500ms for real-time UI
    xTaskCreatePinnedToCore([](void *p) {
        TickType_t xLastWakeTime = xTaskGetTickCount();
        constexpr TickType_t xFrequency = pdMS_TO_TICKS(500);  // 500ms interval for real-time
        
        for(;;) {
            yield();  // Feed watchdog
            currentSensor.updateContinuousReading();  // Fast ~5ms, updates cachedAmps
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    }, "AmpTask", 2048, NULL, 1, NULL, 0);  // Priority 1, Core 0

    // ===== CORE 0: UI SYNC TASK (Priority 1) =====
    // Broadcasts telemetry to WebSocket clients - fast for Pi proxy
    xTaskCreatePinnedToCore([](void *p) {
        TickType_t xLastWakeTime = xTaskGetTickCount();
        constexpr TickType_t xFrequency = pdMS_TO_TICKS(500); // 500ms for real-time UI via Pi proxy
        static unsigned long lastPing = 0;
        
        for(;;) {
            yield();  // Feed watchdog
            
            // Clean up stale WebSocket clients first
            web.ws.cleanupClients();
            
            // Send WebSocket ping every 10 seconds to keep connections alive
            if(millis() - lastPing > 10000 && web.ws.count() > 0) {
                web.ws.pingAll();  // Send ping frames to all clients
                lastPing = millis();
            }
            
            // Send cached weather to new clients (flag set by handleSocketConnect)
            if(pendingCacheBroadcast && cachedWeatherJson.length() > 0 && web.ws.count() > 0) {
                pendingCacheBroadcast = false;
                web.broadcastStatus(cachedWeatherJson);
                Serial.println("[SYNC] Sent cached weather to clients");
            }
            
            // Skip sync if no WebSocket clients connected
            // This saves CPU when nobody is watching
            if(web.ws.count() == 0) {
                vTaskDelayUntil(&xLastWakeTime, xFrequency);
                continue;
            }
            
            JsonDocument sync;
            sync["type"] = "sync";
            sync["ts"] = millis();  // Heartbeat timestamp for stale detection
            
            // Temperature sensor values from DeviceManager
            JsonObject sensorsInfo = sync["sensors"].to<JsonObject>();
            for(const auto &d : deviceMgr.devices) {
                if(d.physicalType == PHYSICAL_DS18B20 && d.lastValue != 0.0f) {
                    sensorsInfo[d.id] = d.lastValue;
                }
            }
            taskYIELD();  // Allow other tasks between sections
            
            // === POWER MONITORING DATA (cached reading for non-blocking UI) ===
            JsonObject power = sync["power"].to<JsonObject>();
            power["total_amps"] = relays.getCachedTotalAmps();  // Cached - non-blocking
            
            // Per-device current data (from delta measurements during relay toggle)
            JsonArray deviceAmps = power["devices"].to<JsonArray>();
            for(const auto &d : deviceMgr.devices) {
                if(d.hardwareChannel > 0 && d.hardwareChannel <= 15) {
                    JsonObject dPower = deviceAmps.add<JsonObject>();
                    dPower["id"] = d.id;
                    dPower["ch"] = d.hardwareChannel;
                    dPower["amps"] = relays.getDeviceAmps(d.hardwareChannel);
                    dPower["on"] = relays.getDeviceState(d.hardwareChannel);
                    dPower["healthy"] = relays.isDeviceHealthy(d.hardwareChannel);
                }
            }
            taskYIELD();  // Allow other tasks
            
            // Config snapshot - use char arrays
            JsonObject cfg = sync["config"].to<JsonObject>();
            cfg["proxy"] = useProxy;
            cfg["piIp"] = piIp;
            cfg["ssid"] = savedSSID;
            cfg["ntp"] = ntpServer;
            cfg["gmt"] = gmtOffset_sec;
            cfg["dst"] = daylightOffset_sec;
            cfg["lat"] = cfgLat;
            cfg["lon"] = cfgLon;
            cfg["city"] = cfgCity;
            cfg["region"] = cfgRegion;
            cfg["unit"] = cfgUnit;
            cfg["ampThresh"] = cfgAmpThreshold;

            // System state
            JsonObject sys = sync["sys"].to<JsonObject>();
            sys["temp"] = currentTemperature;

            // Time
            struct tm timeinfo;
            if(getLocalTime(&timeinfo)) {
                char timeStringBuff[50];
                strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
                sys["time"] = String(timeStringBuff);
                sys["valid"] = true;
            } else {
                sys["time"] = "--:--:--";
                sys["valid"] = false;
            }
            taskYIELD();  // Allow other tasks

            // Network info
            JsonObject net = sync["net"].to<JsonObject>();
            const bool connected = (WiFi.status() == WL_CONNECTED);
            net["connected"] = connected;
            
            if (connected) {
                net["ssid"] = WiFi.SSID();
                net["ip"] = WiFi.localIP().toString();
                net["mask"] = WiFi.subnetMask().toString();
                net["gw"] = WiFi.gatewayIP().toString();
                net["rssi"] = WiFi.RSSI();
                net["mac"] = WiFi.macAddress();
            } else {
                net["ip"] = "0.0.0.0";
            }
            
            JsonArray devArr = sync["devices"].to<JsonArray>();
            deviceMgr.toJson(devArr);
            taskYIELD();  // Allow other tasks before serialization
            
            // LOG device count periodically
            static unsigned long lastDeviceLog = 0;
            if (millis() - lastDeviceLog > 30000) { // Every 30 seconds
                Serial.printf("[SYNC] Broadcasting sync with %d devices\n", deviceMgr.devices.size());
                lastDeviceLog = millis();
            }

            String out;
            serializeJson(sync, out);
            
            // Broadcast in chunks if message is large (prevents blocking)
            web.broadcastStatus(out);
            
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    }, "SyncTask", 12288, NULL, 1, NULL, 0);  // Priority 1, Core 0

    // ===== CORE 1: NETWORK & BACKGROUND TASK (Priority 1) =====
    // Handles WiFi, weather, routines, alerts - all network-bound operations
    xTaskCreatePinnedToCore([](void *p) {
        TickType_t xLastWakeTime = xTaskGetTickCount();
        constexpr TickType_t xFrequency = pdMS_TO_TICKS(100); // 100ms
        unsigned long lastHeapLog = 0;
        
        for(;;) {
            // Feed watchdog and log heap periodically
            yield();
            
            // Handle WiFi Provisioning State Machine
            handleWiFiProvisioning();
            
            if(millis() - lastHeapLog > 60000) { // Every 60 seconds
                Serial.printf("[HEAP] Free: %d bytes, Min: %d bytes\n", 
                    ESP.getFreeHeap(), ESP.getMinFreeHeap());
                lastHeapLog = millis();
            }
            
            // WiFi Scan (Async)
            if (scanRequested) {
                scanRequested = false;
                Serial.println("[WIFI] Starting async scan...");
                WiFi.scanNetworks(true);
            }

            // Check Scan Result
            const int scanResult = WiFi.scanComplete();
            if(scanResult >= 0) {
                Serial.printf("[WIFI] Found %d networks\n", scanResult);
                
                JsonDocument res;
                res["type"] = "wifi_scan_result";
                JsonArray arr = res["networks"].to<JsonArray>();
                
                for(int i = 0; i < scanResult && i < 20; i++) {
                    JsonObject net = arr.add<JsonObject>();
                    net["ssid"] = WiFi.SSID(i);
                    net["rssi"] = WiFi.RSSI(i);
                    net["encrypted"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                }
                
                String out;
                serializeJson(res, out);
                web.broadcastStatus(out);
                WiFi.scanDelete();
            }

            // WiFi Reconnection Logic
            if(!isAPMode && WiFi.status() != WL_CONNECTED) {
                unsigned long currentTime = millis();
                if(currentTime - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
                    lastWiFiCheck = currentTime;
                    wifiReconnectAttempts++;
                    
                    if(wifiReconnectAttempts <= MAX_WIFI_RECONNECT_ATTEMPTS) {
                        Serial.printf("[WIFI] Connection lost! Reconnecting (attempt %d/%d)...\n", 
                                    wifiReconnectAttempts, MAX_WIFI_RECONNECT_ATTEMPTS);
                        
                        if(strlen(savedSSID) > 0) {
                            Preferences prefs;
                            prefs.begin("gh-config", true);
                            String pass = prefs.getString("pass", "");
                            prefs.end();
                            WiFi.begin(savedSSID, pass.c_str());
                        } else {
                            WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
                        }
                        
                        unsigned long startAttempt = millis();
                        while(WiFi.status() != WL_CONNECTED && (millis() - startAttempt) < 15000) {
                            yield();
                            vTaskDelay(pdMS_TO_TICKS(500));
                        }
                        
                        if(WiFi.status() == WL_CONNECTED) {
                            Serial.printf("\n[WIFI] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
                            wifiReconnectAttempts = 0;
                            lastWeatherUpdate = 0;
                            // Register device with Pi server
                            registerDeviceWithPi();
                        }
                    } else {
                        Serial.println("[WIFI] Max reconnection attempts reached. Switching to AP-only mode.");
                        isAPMode = true;
                        web.broadcastStatus("{\"type\":\"net_status\",\"connected\":false,\"ap_mode\":true}");
                    }
                }
            } else if(WiFi.status() == WL_CONNECTED && wifiReconnectAttempts > 0) {
                wifiReconnectAttempts = 0;
            }

            // Sync Settings from Pi Server (if connected)
            if(!isAPMode && WiFi.status() == WL_CONNECTED) {
                syncSettingsFromPi();
            }
            
            // Check for IP address changes and re-register if needed
            checkIPAddressChange();

            // Pending weather refresh (triggered 5s after WebSocket connect) - ONLY if Pi is NOT connected
            if(pendingWeatherRefresh > 0 && millis() >= pendingWeatherRefresh) {
                pendingWeatherRefresh = 0;  // Clear flag
                if(!proxyConnected && !isAPMode && WiFi.status() == WL_CONNECTED) {
                    Serial.println("[Weather] Executing delayed refresh after client connect");
                    fetchWeather();
                    lastWeatherUpdate = millis();
                }
            }

            // Weather Update (30 min interval) - PAUSED when Pi proxy is connected
            // When Pi is up, it handles all weather fetching. We only fetch when Pi is down.
            if(!proxyConnected && !isAPMode && WiFi.status() == WL_CONNECTED) {
                if(lastWeatherUpdate == 0 || (millis() - lastWeatherUpdate) > WEATHER_UPDATE_INTERVAL) {
                    Serial.println("[Weather] ⏯️  RESUMED: Pi proxy disconnected, resuming local weather checks.");
                    fetchWeather();
                    lastWeatherUpdate = millis();
                }
            } else if(proxyConnected) {
                // When Pi proxy is up, reset weather update timer to prevent stale data checks
                // The UI will get weather from Pi proxy instead
                if(lastWeatherUpdate > 0) {
                    lastWeatherUpdate = millis();  // Keep timer fresh while paused
                }
            }
            
            // Routine Trigger Checking (every 60 seconds)
            if(millis() - lastRoutineCheck >= ROUTINE_CHECK_INTERVAL) {
                lastRoutineCheck = millis();
                
                // Calculate average temperature from DeviceManager (already updated by TempTask)
                float tempSum = 0.0f;
                int tempCount = 0;
                
                for(const auto &d : deviceMgr.devices) {
                    if(d.physicalType == PHYSICAL_DS18B20 && d.lastValue != 0.0f) {
                        tempSum += d.lastValue;
                        tempCount++;
                    }
                }
                
                float avgTemp = (tempCount > 0) ? (tempSum / tempCount) : currentTemperature;
                
                // Get current time for cron scheduling
                struct tm timeinfo;
                int hour = 0, minute = 0, dayOfWeek = 0, dayOfMonth = 0, month = 0;
                if(getLocalTime(&timeinfo)) {
                    hour = timeinfo.tm_hour;
                    minute = timeinfo.tm_min;
                    dayOfWeek = timeinfo.tm_wday;
                    dayOfMonth = timeinfo.tm_mday;
                    month = timeinfo.tm_mon + 1;
                }
                
                Serial.printf("[ROUTINE] Checking triggers: avgTemp=%.2f°C, weatherTemp=%.2f°C\n", 
                             avgTemp, lastWeatherTemp);
                
                // Check all routine triggers (may trigger relay operations - handled via WebSocket)
                routineMgr.checkTriggers(avgTemp, lastWeatherTemp, deviceMgr, relays, 
                                        hour, minute, dayOfWeek, dayOfMonth, month);
                
                // === ALERT SYSTEM CHECKS ===
                alertMgr.checkConnection(WiFi.status() == WL_CONNECTED);
                
                uint16_t activeRelayMask = 0;
                for(const auto &d : deviceMgr.devices) {
                    if(d.active && d.enabled && d.hardwareChannel > 0 && d.hardwareChannel <= 15) {
                        activeRelayMask |= (1 << (d.hardwareChannel - 1));
                    }
                }
                alertMgr.checkUnexpectedCurrent(relays.getTotalAmps(), activeRelayMask);
                
                bool heatingActive = false, coolingActive = false;
                for(const auto &d : deviceMgr.devices) {
                    if(d.active && d.enabled && d.hardwareChannel > 0) {
                        String typeLower = d.type;
                        typeLower.toLowerCase();
                        if(typeLower.indexOf("heat") >= 0) heatingActive = true;
                        if(typeLower.indexOf("cool") >= 0 || typeLower.indexOf("fan") >= 0) coolingActive = true;
                    }
                }
                alertMgr.checkTemperatureAnomaly(avgTemp, 25.0f, heatingActive, coolingActive);
                alertMgr.checkFrostNow(avgTemp, 2.0f);
                
                for(const auto &d : deviceMgr.devices) {
                    if(d.active && d.enabled && d.hardwareChannel > 0) {
                        String typeLower = d.type;
                        typeLower.toLowerCase();
                        bool isLamp = typeLower.indexOf("light") >= 0 || typeLower.indexOf("lamp") >= 0;
                        alertMgr.checkLampDuration(d.hardwareChannel, d.name, isLamp);
                    }
                }
                
                // Daily summary at 8 AM
                if(hour == 8 && minute == 0) {
                    alertMgr.sendDailySummary(avgTemp, 0.0f, 0.0f, 0, 0);
                }
            }
            
            // Process async routine execution
            auto progressCallback = [](const String& id, int step, int total, ExecutionStatus status) {
                JsonDocument msg;
                msg["type"] = "routine_progress";
                msg["id"] = id;
                msg["step"] = step;
                msg["total"] = total;
                msg["status"] = static_cast<int>(status);
                String out;
                serializeJson(msg, out);
                web.broadcastStatus(out);
            };
            routineMgr.processRoutines(deviceMgr, relays, progressCallback);
            
            // Process alert message queue
            alertMgr.processQueue();

            if(isAPMode) dnsServer.processNextRequest();
            web.cleanup();
            
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
        }
    }, "NetTask", 12288, NULL, 1, NULL, 1);  // Priority 1, Core 1
}

void loop() { vTaskDelay(portMAX_DELAY); }