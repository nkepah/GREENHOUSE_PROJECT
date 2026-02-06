#ifndef ALERT_MANAGER_H
#define ALERT_MANAGER_H

#include <Arduino.h>
#include <string>

class AlertManager {
public:
    AlertManager();
    
    void init();
    void sendAlert(const String& message);
    void sendAlert(const String& title, const String& message);
    
private:
    bool initialized;
};

#endif // ALERT_MANAGER_H
