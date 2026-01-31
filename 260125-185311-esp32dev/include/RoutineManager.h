#ifndef ROUTINE_MANAGER_H
#define ROUTINE_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include "DeviceManager.h"
#include "RelayController.h"

struct DeviceConfirmResult {
    String deviceId;
    String deviceName;
    int channel;
    bool targetState;
    float deltaAmps;
    bool confirmed;
};

class RoutineManager {
public:
    RoutineManager();
    
    void init();
    void setAmpThreshold(float threshold);
    void setFailureCallback(std::function<void(const String&, const std::vector<DeviceConfirmResult>&)> cb);
    void checkTriggers(float temp, float weatherTemp, DeviceManager& devMgr, RelayController& relays,
                       int hour, int minute, int dayOfWeek, int dayOfMonth, int month);
    void processRoutines(DeviceManager& devMgr, RelayController& relays,
                        std::function<void(const String&, int, int, int)> progressCallback);
    void startRoutineByName(const String& routineName);
    
private:
    float ampThreshold;
    std::function<void(const String&, const std::vector<DeviceConfirmResult>&)> failureCallback;
};

#endif
