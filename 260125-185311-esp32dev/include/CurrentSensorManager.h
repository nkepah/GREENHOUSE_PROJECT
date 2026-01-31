#ifndef CURRENT_SENSOR_MANAGER_H
#define CURRENT_SENSOR_MANAGER_H

#include <Arduino.h>

class CurrentSensorManager {
public:
    CurrentSensorManager();
    
    void begin(int analogPin);
    float readAmps();
    float getCurrentDelta();
    void reset();
    
private:
    int sensorPin;
    float lastReading;
    float calibrationFactor;
};

#endif
