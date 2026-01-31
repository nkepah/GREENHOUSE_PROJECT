/**
 * 🐔 Chicken Coop Controller - ESP32 Firmware Template
 * 
 * Based on the Greenhouse automation system.
 * Controls: Lights, Door Actuator, Feed Dispenser, Temperature Monitoring
 * 
 * Features:
 * - Automatic dawn/dusk lighting
 * - Scheduled door opening/closing
 * - Timed feed dispensing
 * - Temperature monitoring
 * - WebSocket real-time control
 * - Weather-aware scheduling (via Pi proxy)
 * 
 * Hardware:
 * - ESP32 Dev Module
 * - DS18B20 Temperature Sensor
 * - Linear Actuator (door)
 * - Servo/DC Motor (feeder)
 * - LED/Relay (lights)
 */

#ifndef COOP_CONFIG_H
#define COOP_CONFIG_H

// =============================================================================
// COOP IDENTIFICATION
// =============================================================================

// Change these for each coop!
#define COOP_ID "coop1"                    // coop1, coop2, coop3
#define COOP_NAME "Coop Alpha"             // Display name
#define COOP_DESCRIPTION "Layer Hens"      // Description

// =============================================================================
// NETWORK CONFIGURATION
// =============================================================================

// WiFi (same as greenhouse, or separate)
#define COOP_SSID "YourWiFiSSID"
#define COOP_PASS "YourWiFiPassword"

// Raspberry Pi Gateway (for weather offloading)
#define PI_GATEWAY_IP "100.92.151.67"      // Updated to your Pi IP
#define PI_GATEWAY_PORT 3000
#define USE_PI_WEATHER true                // Get weather from Pi instead of direct API

// Access Point (for direct config)
#define COOP_AP_SSID "ChickenCoop-Setup"
#define COOP_AP_PASS "coop12345"

// =============================================================================
// GPIO PIN MAPPING
// =============================================================================

// Lighting Control
#define PIN_LIGHT_MAIN 2                   // Main coop light
#define PIN_LIGHT_NESTING 4                // Nesting box light (dimmer)
#define PIN_LIGHT_RUN 5                    // Outdoor run light

// Door Actuator (Linear Actuator)
#define PIN_DOOR_EXTEND 12                 // Actuator extend (open)
#define PIN_DOOR_RETRACT 13                // Actuator retract (close)
#define PIN_DOOR_LIMIT_OPEN 25             // Limit switch - door open
#define PIN_DOOR_LIMIT_CLOSE 26            // Limit switch - door closed

// Feed Dispenser
#define PIN_FEEDER_MOTOR 14                // Feeder motor/servo
#define PIN_FEEDER_LEVEL 34                // Feed level sensor (analog)

// Sensors
#define PIN_TEMP_SENSOR 32                 // DS18B20 data pin (inside coop)
#define PIN_TEMP_OUTSIDE 33                // DS18B20 outside (optional)
#define PIN_WATER_LEVEL 35                 // Water level sensor (analog)
#define PIN_LIGHT_SENSOR 36                // LDR for ambient light

// =============================================================================
// TIMING DEFAULTS (can be changed via web UI)
// =============================================================================

// Door Schedule (24h format, -1 = use sunrise/sunset)
#define DOOR_OPEN_HOUR -1                  // -1 = sunrise
#define DOOR_CLOSE_HOUR -1                 // -1 = sunset
#define DOOR_SUNRISE_OFFSET 30             // Minutes after sunrise
#define DOOR_SUNSET_OFFSET -30             // Minutes before sunset

// Lighting Schedule
#define LIGHT_ON_HOUR 5                    // 5:00 AM
#define LIGHT_OFF_HOUR 21                  // 9:00 PM
#define LIGHT_MIN_DAYLIGHT_HOURS 14        // Extend to 14 hours for egg production

// Feeding Schedule
#define FEED_TIME_1_HOUR 6                 // Morning feed
#define FEED_TIME_1_MIN 0
#define FEED_TIME_2_HOUR 18                // Evening feed
#define FEED_TIME_2_MIN 0
#define FEED_DURATION_SEC 5                // How long to run feeder motor

// =============================================================================
// SAFETY LIMITS
// =============================================================================

#define TEMP_MIN_SAFE 5.0                  // °C - below this, alert!
#define TEMP_MAX_SAFE 35.0                 // °C - above this, alert!
#define WATER_MIN_LEVEL 20                 // % - below this, alert!
#define FEED_MIN_LEVEL 10                  // % - below this, alert!

// =============================================================================
// DEVICE TYPES (for unified dashboard)
// =============================================================================

enum CoopDeviceType {
    DEVICE_LIGHT = 0,
    DEVICE_DOOR = 1,
    DEVICE_FEEDER = 2,
    DEVICE_SENSOR = 3,
    DEVICE_WATER = 4
};

// =============================================================================
// DOOR STATES
// =============================================================================

enum DoorState {
    DOOR_UNKNOWN = 0,
    DOOR_OPEN = 1,
    DOOR_CLOSED = 2,
    DOOR_OPENING = 3,
    DOOR_CLOSING = 4,
    DOOR_ERROR = 5
};

#endif // COOP_CONFIG_H
