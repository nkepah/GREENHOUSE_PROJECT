#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <HTTPClient.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <algorithm>

// Physical device types
enum PhysicalDeviceType {
    PHYSICAL_NONE = 0,
    PHYSICAL_RELAY = 1,        // Channels 1-15
    PHYSICAL_DHT22 = 2,        // Temperature & Humidity
    PHYSICAL_DS18B20 = 3,      // Temperature only
    PHYSICAL_CAMERA_IP = 4,    // IP Camera (RTSP/HTTP)
    PHYSICAL_CAMERA_ESP32 = 5  // ESP32-CAM module
};

struct Device {
    String id;
    String type;
    String name;
    int16_t x;           // Percent - desktop position (int16_t saves RAM)
    int16_t y;
    int16_t x_mobile;
    int16_t y_mobile;
    int16_t rotation;    // Icon rotation in degrees
    int16_t rotation_mobile;
    bool active;
    uint8_t hardwareChannel; // 0=None, 1-15=Relay (uint8_t saves RAM)
    bool enabled;
    
    // Extended physical device fields
    PhysicalDeviceType physicalType = PHYSICAL_NONE;
    String physicalAddress;  // Sensor address (OneWire) or Camera IP/URL
    int8_t physicalPin = -1; // GPIO pin for DHT22 or data pin (int8_t saves RAM)
    float lastValue = 0.0f;
};

class DeviceManager {
public:
    std::vector<Device> devices;

    void begin() {
        bool loaded = loadLayout();
        if (!loaded) {
            Serial.println(F("[NVS] WARNING: Failed to load layout from NVS"));
            Serial.println(F("[NVS] Keeping devices in memory (if any)"));
        }
    }

    void createDefaultLayout() {
        Serial.println(F("[NVS] WARNING: createDefaultLayout() called!"));
        devices.clear();
        devices.shrink_to_fit();
        saveLayout();
        Serial.println(F("[NVS] Default layout saved (0 devices)"));
    }

    void createDevice(const String &type, int x, int y) {
        const String id = type + String(random(1000, 9999));
        add(id, type, "New Device", x, y, 0);
        saveLayout();
    }
    
    // Get device by ID (const pointer, returns nullptr if not found)
    const Device* getDevice(const String &id) const {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
        return (it != devices.end()) ? &(*it) : nullptr;
    }

    void updateDetails(const String &id, const String &name, int channel) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->name = name;
            it->hardwareChannel = channel;
            saveLayout();
        }
    }
    
    // Extended update for physical sensors/cameras
    void updatePhysicalDevice(const String &id, const String &name, int channel, 
                             PhysicalDeviceType physType, const String &address, int pin) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->name = name;
            it->hardwareChannel = channel;
            it->physicalType = physType;
            it->physicalAddress = address;
            it->physicalPin = pin;
            saveLayout();
        }
    }
    
    void updateRotation(const String &id, int rotation) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->rotation = rotation;
            saveLayout();
        }
    }
    
    void updateRotationMobile(const String &id, int rotation_mobile) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->rotation_mobile = rotation_mobile;
            saveLayout();
        }
    }
    
    // Update sensor reading
    void updateSensorValue(const String &id, float value) {
        auto it = std::find_if(devices.begin(), devices.end(), 
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->lastValue = value;
            it->active = true; // Mark as active when receiving data
        }
    }

    void deleteDevice(const String &id) {
        auto it = std::remove_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            devices.erase(it, devices.end());
            saveLayout();
        }
    }

    void updatePosition(const String &id, int x, int y) {
        auto it = std::find_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->x = x;
            it->y = y;
            saveLayout();
        }
    }
    
    void updateMobilePosition(const String &id, int x_mobile, int y_mobile) {
        auto it = std::find_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->x_mobile = x_mobile;
            it->y_mobile = y_mobile;
            saveLayout();
        }
    }
    
    // Alias for updatePosition for consistency with frontend
    void moveDevice(const String &id, int x, int y) {
        updatePosition(id, x, y);
    }

    void setEnabled(const String &id, bool enabled) {
        auto it = std::find_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it != devices.end()) {
            it->enabled = enabled;
            saveLayout();
        }
    }

    void toJson(JsonArray &arr) const {
        for(const auto &d : devices) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = d.id;
            obj["type"] = d.type;
            obj["name"] = d.name;
            obj["x"] = (int)d.x;
            obj["y"] = (int)d.y;
            obj["x_mobile"] = (int)d.x_mobile;
            obj["y_mobile"] = (int)d.y_mobile;
            obj["rotation"] = (int)d.rotation;
            obj["rotation_mobile"] = (int)d.rotation_mobile;
            obj["state"] = d.active;
            obj["ch"] = (int)d.hardwareChannel;
            obj["enabled"] = d.enabled;
            
            // Physical device info (only if set)
            if(d.physicalType != PHYSICAL_NONE) {
                obj["phys_type"] = static_cast<int>(d.physicalType);
                if(d.physicalAddress.length() > 0) {
                    obj["phys_addr"] = d.physicalAddress;
                }
                if(d.physicalPin >= 0) {
                    obj["phys_pin"] = (int)d.physicalPin;
                }
            }
            
            // Include sensor reading if available
            if(d.lastValue != 0.0f && d.physicalType >= PHYSICAL_DHT22 && d.physicalType <= PHYSICAL_DS18B20) {
                obj["value"] = d.lastValue;
            }
        }
    }

    // Toggle with channel sync - optimized with modern C++
    int toggle(const String &id) {
        auto it = std::find_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it == devices.end() || !it->enabled) return 0;
        
        const bool newState = !it->active;
        const int targetChannel = it->hardwareChannel;
        
        // Sync all devices on same channel
        if(targetChannel > 0) {
            for(auto &d : devices) {
                if(d.hardwareChannel == targetChannel) {
                    d.active = newState;
                }
            }
        } else {
            it->active = newState;
        }
        
        saveLayout();
        return targetChannel;
    }

    // Explicit state setter with channel sync
    int setState(const String &id, bool state) {
        auto it = std::find_if(devices.begin(), devices.end(),
            [&id](const Device &d) { return d.id == id; });
            
        if(it == devices.end() || (!it->enabled && state)) return 0;
        
        const int targetChannel = it->hardwareChannel;
        
        if(targetChannel > 0) {
            for(auto &d : devices) {
                if(d.hardwareChannel == targetChannel) {
                    d.active = state;
                }
            }
        } else {
            it->active = state;
        }
        
        saveLayout();
        return targetChannel;
    }

    void saveLayout() {
        Serial.println(F("[FS] Saving to LittleFS..."));
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        
        for(const auto &d : devices) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = d.id;
            obj["type"] = d.type;
            obj["name"] = d.name;
            obj["x"] = (int)d.x;
            obj["y"] = (int)d.y;
            obj["x_mobile"] = (int)d.x_mobile;
            obj["y_mobile"] = (int)d.y_mobile;
            obj["rotation"] = (int)d.rotation;
            obj["rotation_mobile"] = (int)d.rotation_mobile;
            obj["ch"] = (int)d.hardwareChannel;
            obj["en"] = d.enabled;
            obj["act"] = d.active;
            
            if(d.physicalType != PHYSICAL_NONE) {
                obj["pt"] = static_cast<int>(d.physicalType);
                if(d.physicalAddress.length() > 0) obj["pa"] = d.physicalAddress;
                if(d.physicalPin >= 0) obj["pp"] = (int)d.physicalPin;
            }
        }

        String out;
        serializeJson(doc, out);
        
        Serial.printf("[FS] Serialized: %d bytes, %d devices\n", out.length(), devices.size());

        // Save to LittleFS file
        File file = LittleFS.open("/layout.json", "w");
        if(!file) {
            Serial.println(F("[FS] ERROR: Cannot open /layout.json!"));
            return;
        }
        
        size_t written = file.print(out);
        file.close();
        
        Serial.printf("[FS] Wrote %d bytes to /layout.json\n", written);
        
        // Quick verification
        File verifyFile = LittleFS.open("/layout.json", "r");
        if(verifyFile) {
            size_t fileSize = verifyFile.size();
            verifyFile.close();
            if(fileSize == out.length()) {
                Serial.println(F("[FS] Verified OK"));
            } else {
                Serial.println(F("[FS] VERIFY FAILED!"));
            }
        }
    }

    bool loadLayout() {
        Serial.println(F("[FS] Loading from LittleFS..."));
        
        // First try LittleFS file
        if(LittleFS.exists("/layout.json")) {
            File file = LittleFS.open("/layout.json", "r");
            if(file) {
                size_t fileSize = file.size();
                Serial.printf("[FS] Found /layout.json (%d bytes)\n", fileSize);
                
                if(fileSize > 0) {
                    String json = file.readString();
                    file.close();
                    
                    if(parseLayout(json)) {
                        Serial.printf("[FS] Loaded %d devices\n", devices.size());
                        return true;
                    }
                    Serial.println(F("[FS] Parse failed"));
                } else {
                    file.close();
                }
            }
        }
        
        // Fall back to NVS (for migration from old storage)
        Preferences prefs;
        if(prefs.begin("greenhouse", true)) {
            uint32_t parts = prefs.getUInt("layoutParts", 0);
            if(parts > 0) {
                Serial.printf("[FS] Migrating %u parts from NVS...\n", parts);
                String json;
                json.reserve(parts * 3000);  // Pre-allocate for efficiency
                if(parts == 1) {
                    json = prefs.getString("layout", "");
                } else {
                    for(uint32_t i = 0; i < parts; i++) {
                        char key[12];
                        snprintf(key, sizeof(key), "layout%u", i);
                        json += prefs.getString(key, "");
                    }
                }
                prefs.end();
                
                if(json.length() > 0 && parseLayout(json)) {
                    Serial.printf("[FS] Migrated %d devices\n", devices.size());
                    saveLayout();
                    // Clean up old NVS keys
                    Preferences cleanPrefs;
                    if(cleanPrefs.begin("greenhouse", false)) {
                        cleanPrefs.clear();  // Clear all keys in namespace
                        cleanPrefs.end();
                    }
                    return true;
                }
            }
            prefs.end();
        }
        
        Serial.println(F("[FS] No layout data found"));
        return false;
    }

    bool parseLayout(const String &json) {
        JsonDocument doc;
        const DeserializationError error = deserializeJson(doc, json);
        
        if (error) {
            Serial.printf("[NVS] JSON Error: %s\n", error.c_str());
            return false;
        }

        devices.clear();
        
        JsonArray arr = doc.as<JsonArray>();
        devices.reserve(arr.size());  // Pre-allocate exact size
        
        for(const JsonObject &obj : arr) {
            Device d;
            d.id = obj["id"].as<String>();
            d.type = obj["type"].as<String>();
            d.name = obj["name"].as<String>();
            d.x = obj["x"] | 50;
            d.y = obj["y"] | 50;
            d.x_mobile = obj["x_mobile"] | d.x;
            d.y_mobile = obj["y_mobile"] | d.y;
            d.rotation = obj["rotation"] | 0;
            d.rotation_mobile = obj["rotation_mobile"] | d.rotation;
            d.hardwareChannel = obj["ch"] | 0;
            d.enabled = obj["en"] | true;
            d.active = obj["act"] | false;
            
            // Load physical device configuration
            if(obj.containsKey("pt")) {
                d.physicalType = static_cast<PhysicalDeviceType>(obj["pt"].as<int>());
                d.physicalAddress = obj["pa"].as<String>();
                d.physicalPin = obj["pp"] | -1;
            }
            
            d.lastValue = 0.0f;
            devices.push_back(std::move(d));  // Use move to avoid copy
        }
        
        Serial.printf("[NVS] Parsed %d devices\n", devices.size());
        return true;
    }

private:
    void add(const String &id, const String &type, const String &name, int x, int y, int ch) {
        Device d;
        d.id = id;
        d.type = type;
        d.name = name;
        d.x = x;
        d.y = y;
        d.x_mobile = x; // Initialize mobile position to desktop position
        d.y_mobile = y; // Initialize mobile position to desktop position
        d.rotation = 0; // Initialize rotation to 0 degrees
        d.rotation_mobile = 0; // Initialize mobile rotation to 0 degrees
        d.active = false;
        d.hardwareChannel = ch;
        d.enabled = true;
        d.physicalType = PHYSICAL_NONE;
        d.physicalAddress = "";
        d.physicalPin = -1;
        d.lastValue = 0.0f;
        devices.push_back(d);
    }
};

extern DeviceManager deviceMgr;

#endif // DEVICE_MANAGER_H
