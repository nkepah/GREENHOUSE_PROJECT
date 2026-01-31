#ifndef WIFI_PROVISIONING_H
#define WIFI_PROVISIONING_H

#include <Arduino.h>
#include <WiFi.h>

class WiFiProvisioning {
public:
    enum WiFiState {
        STATE_INIT,
        STATE_CONNECTING,
        STATE_HANDSHAKE,
        STATE_AP_MODE,
        STATE_READY,
        STATE_FAILED
    };
    
    enum DeviceType {
        GENERIC = 0,
        GREENHOUSE = 1,
        CHICKEN_COOP = 2
    };
    
    static void begin(DeviceType type = GENERIC);
    static void update();
    
    // Getters
    static String getDeviceID();
    static WiFiState getState() { return currentState; }
    
    // Network methods
    static bool tryConnection(const char* ssid, const char* password);
    static bool connectToSavedNetwork();
    static bool connectToDefaultNetwork();
    static void startAPMode();
    
    // Pi communication
    static bool handshakeWithPi(const char* piAddress);
    static bool notifyPi(const char* piAddress);
    static bool reportNetworkStatus(const char* piAddress);
    static bool checkAndDownloadOTA(const char* piAddress);
    
private:
    // State management
    static WiFiState currentState;
    static DeviceType deviceType;
    static unsigned long stateStartTime;
    static uint8_t connectionAttempts;
    static unsigned long lastNetworkStatusReport;
    static unsigned long lastOTACheck;
    
    // Transition helpers
    static void transitionToConnecting();
    static void transitionToHandshake();
    static void transitionToReady();
    static void transitionToAPMode();
    static void transitionToFailed();
    static void handleAPModeUpdate();
    
    // Timeouts and limits
    static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
    static const unsigned long HANDSHAKE_TIMEOUT_MS = 5000;
    static const unsigned long AP_MODE_TIMEOUT_MS = 600000; // 10 minutes
    static const uint8_t MAX_ATTEMPTS = 3;
};

#endif
