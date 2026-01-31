#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>
#include "CurrentSensorManager.h"

class RelayController {
public:
    RelayController();
    
    void begin();
    void pulseRelay(uint8_t channel);
    void setRelayState(uint8_t channel, bool state);
    bool getRelayState(uint8_t channel);
    void attachCurrentSensor(CurrentSensorManager* sensor);
    void syncDeviceState(uint8_t channel, bool state);
    float getTotalAmps();
    void setAmpThreshold(float threshold);
    
private:
    CurrentSensorManager* currentSensor;
    float ampThreshold;
};

#endif
