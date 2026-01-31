#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <tuple>
#include <functional>
#include "DeviceManager.h"

class AlertManager {
public:
    AlertManager();
    
    void begin();
    void setDeviceManager(DeviceManager* devMgr);
    void setRoutineCallback(std::function<void(const String&)> cb);
    void sendRebootAlert(const String& ipAddress);
    void alertRoutineDeviceFailures(const String& routineName,
                                    const std::vector<std::tuple<String, String, int, bool, float, bool>>& results);
    void checkConnection(bool connected);
    void checkUnexpectedCurrent(float totalAmps, uint16_t activeRelayMask);
    void processQueue();
    
private:
    DeviceManager* deviceMgr;
    std::function<void(const String&)> routineCallback;
};

#endif
