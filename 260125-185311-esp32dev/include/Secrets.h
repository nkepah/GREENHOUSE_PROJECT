#ifndef SECRETS_H
#define SECRETS_H

/**
 * Secrets and Configuration Constants
 * This file holds static configuration for the ESP32 greenhouse controller
 * 
 * NETWORK ARCHITECTURE:
 * ────────────────────────────────────────────────────────────────────────
 * ESP32 (field device) ←→ farm-hub (Pi server)
 * 
 * Connection Method: mDNS hostname resolution (recommended)
 *   • Pi Hostname: farm-hub
 *   • mDNS Domain: farm-hub.local
 *   • Local Network IP: 10.0.0.3
 *   • Tailscale VPN IP: 100.92.151.67
 *   
 * Benefit: If Pi's IP address changes, ESP32 automatically reconnects
 *          using the persistent hostname (farm-hub)
 * 
 * ESP32 Connection Flow:
 *   1. Connects to WiFi: "Baminyam2.0_EXT2.4G"
 *   2. Resolves PI_HOSTNAME ("farm-hub") via mDNS
 *   3. Connects to farm-hub:3000
 *   4. Registers device IP every 30 seconds
 *   5. Receives routine commands via WebSocket
 *   6. Sends sensor data and alerts to Pi
 */

// Device Configuration
static constexpr const char* DEVICE_NAME = "greenhouse";
static constexpr const char* DEVICE_TYPE = "greenhouse";
static constexpr const char* FIRMWARE_VERSION = "2.0.0";

// WiFi Provisioning Portal
static constexpr const char* AP_SSID = "Greenhouse-Setup";
static constexpr const char* AP_PASSWORD = "greenhouse123";  // TODO: Make random or use device MAC

// Default Network Credentials
static constexpr const char* DEFAULT_SSID = "Baminyam2.0_EXT2.4G";
static constexpr const char* DEFAULT_PASS = "Jesus2023";

// Raspberry Pi Configuration (farm-hub server)
// Using hostname instead of IP ensures persistent connection even if Pi IP changes
// mDNS Resolution: farm-hub → 10.0.0.3 (local network)
// Direct IP (Tailscale): 100.92.151.67
// mDNS Domain: farm-hub.local
static constexpr const char* PI_HOSTNAME = "farm-hub";      // Resolves via mDNS to 10.0.0.3
static constexpr const uint16_t PI_PORT = 3000;             // Node.js backend port
static constexpr const char* PI_DOMAIN = "farm-hub.local";  // Full mDNS domain (optional)

// OTA Update Server
static constexpr const char* OTA_SERVER = "updates.farm.local";
static constexpr const uint16_t OTA_PORT = 8888;

// Sensor Calibration
static constexpr float CURRENT_SENSOR_OFFSET = 0.0f;    // ACS712 offset (0A reading)
static constexpr float CURRENT_SENSOR_SENSITIVITY = 0.185f;  // 185mV per A for ACS712-5A

// Temperature Sensor Configuration
static constexpr uint8_t TEMP_SENSOR_RESOLUTION = 12;  // 12-bit = 0.0625°C precision
static constexpr uint16_t TEMP_CONVERSION_TIME_MS = 750;  // 12-bit takes 750ms

// Task Configuration
static constexpr uint16_t TEMPERATURE_TASK_STACK = 4096;
static constexpr uint8_t TEMPERATURE_TASK_PRIORITY = 2;
static constexpr uint8_t TEMPERATURE_TASK_CORE = 0;

static constexpr uint16_t RELAY_TASK_STACK = 3072;
static constexpr uint8_t RELAY_TASK_PRIORITY = 3;
static constexpr uint8_t RELAY_TASK_CORE = 0;

static constexpr uint16_t WEB_TASK_STACK = 8192;
static constexpr uint8_t WEB_TASK_PRIORITY = 1;
static constexpr uint8_t WEB_TASK_CORE = 0;

static constexpr uint16_t DEVICE_REG_TASK_STACK = 4096;
static constexpr uint8_t DEVICE_REG_TASK_PRIORITY = 0;
static constexpr uint8_t DEVICE_REG_TASK_CORE = 1;

#endif // SECRETS_H
