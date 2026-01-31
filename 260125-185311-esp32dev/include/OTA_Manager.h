#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class OTAManager {
public:
    static void begin(AsyncWebServer* server);
    static void confirmUpdate();
    static void checkForUpdates();
    
private:
    static AsyncWebServer* webServer;
};

#endif
