#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

struct Device {
    String id;
    String name;
    String type;
    int hardwareChannel;
    bool active;
    bool enabled;
    float minTemp;
    float maxTemp;
};

class DeviceManager {
public:
    DeviceManager();
    
    void begin();
    void addDevice(const Device& device);
    void removeDevice(const String& deviceId);
    void toJson(JsonArray& arr) const;
    
    std::vector<Device> devices;
    
private:
};

#endif
