#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>

// Forward declaration - CurrentSensorManager is optional
class CurrentSensorManager;

/**
 * RelayController with Delta-based Current Sensing
 * 
 * When a CurrentSensorManager is attached, this controller captures
 * current readings BEFORE and AFTER each relay toggle to determine
 * the individual device's power draw (delta).
 * 
 * This allows single-sensor monitoring of multiple devices!
 */
class RelayController {
private:
    static constexpr int LATCH_PIN = 12;  // RCLK
    static constexpr int DATA_PIN = 13;   // SERIN
    static constexpr int CLOCK_PIN = 14;  // SRCLK
    static constexpr int OE_PIN = 33;     // Output Enable
    static constexpr int FAN_BIT = 7;     // SR Bit 7 for Fan MOSFET

    // Relay mapping - inline for C++11 compatibility
    // Maps relay number (1-15) to shift register bit position
    inline static const int RELAY_MAP[15] = {14, 2, 1, 3, 5, 6, 4, 11, 10, 0, 12, 13, 13, 8, 6};

    uint16_t currentRegisterState = 0x0000;
    
    // Current sensing
    CurrentSensorManager* currentSensor = nullptr;
    float deviceCurrents[16] = {0};      // Delta current for each relay channel (0-15)
    bool deviceStates[16] = {false};     // Track ON/OFF state for each channel
    unsigned long lastToggleTime[16] = {0}; // Timestamp of last toggle
    
    // Configuration
    static constexpr int SETTLE_TIME_MS = 60;    // Time for inrush to settle (ms)
    // Noise-aware threshold: User observed max 0.23A noise with no device connected
    // Set threshold above 0.23A to avoid false positive confirmations
    // Note: Now configurable at runtime via setAmpThreshold()
    float minDeltaThreshold = 0.25f;  // Default threshold, can be changed via config

public:
    RelayController() = default;

    void begin() {
        pinMode(OE_PIN, OUTPUT);
        digitalWrite(OE_PIN, HIGH); // Lock during init

        pinMode(LATCH_PIN, OUTPUT);
        pinMode(DATA_PIN, OUTPUT);
        pinMode(CLOCK_PIN, OUTPUT);

        // Initialize state tracking
        memset(deviceCurrents, 0, sizeof(deviceCurrents));
        memset(deviceStates, 0, sizeof(deviceStates));
        memset(lastToggleTime, 0, sizeof(lastToggleTime));

        // Calibration sequence
        for (uint8_t i = 0; i < 3; i++) {
            updateShiftRegisters(0xFFFF);
            vTaskDelay(pdMS_TO_TICKS(150));
            updateShiftRegisters(0x0000);
            vTaskDelay(pdMS_TO_TICKS(150));
        }

        digitalWrite(OE_PIN, LOW);
        Serial.println(F("[Relay] Initialized"));
    }

    /**
     * Attach a current sensor for delta-based monitoring
     */
    void attachCurrentSensor(CurrentSensorManager* sensor) {
        currentSensor = sensor;
        Serial.println(F("[Relay] Current sensor attached"));
    }

    void setAmpThreshold(float threshold) {
        minDeltaThreshold = threshold;
    }

    /**
     * Get current amp threshold
     */
    float getAmpThreshold() const {
        return minDeltaThreshold;
    }

    /**
     * Pulse relay with optional delta current measurement
     * @param relayNum Relay number 1-15
     * @return The measured delta current (0 if no sensor attached)
     */
    float pulseRelay(int relayNum) {
        if (relayNum < 1 || relayNum > 15) return 0.0f;
        
        const int targetBit = RELAY_MAP[relayNum - 1];
        float delta = 0.0f;
        
        // === STEP 1: Capture baseline current BEFORE toggle ===
        float baseline = 0.0f;
        if (currentSensor) {
            baseline = getMainLineAmps();
            Serial.printf("[Relay] CH%d Baseline: %.2fA\n", relayNum, baseline);
        }

        // === STEP 2: Pulse the relay ===
        // Pulse ON
        currentRegisterState |= (1 << targetBit);
        updateShiftRegisters(currentRegisterState);

        vTaskDelay(pdMS_TO_TICKS(100)); // Pulse duration for latching relay

        // Pulse OFF (latching relay retains its toggled state)
        currentRegisterState &= ~(1 << targetBit);
        updateShiftRegisters(currentRegisterState);
        
        // === STEP 3: Wait for inrush/transient to settle ===
        vTaskDelay(pdMS_TO_TICKS(SETTLE_TIME_MS));
        
        // === STEP 4: Capture final current AFTER toggle ===
        if (currentSensor) {
            float final_amps = getMainLineAmps();
            Serial.printf("[Relay] CH%d Final: %.2fA\n", relayNum, final_amps);
            
            // === STEP 5: Calculate and store delta ===
            delta = abs(final_amps - baseline);
            
            // Apply noise threshold
            if (delta < minDeltaThreshold) {
                delta = 0.0f;
            }
            
            // Update device tracking
            bool wasOn = deviceStates[relayNum];
            deviceStates[relayNum] = !wasOn;  // Toggle state
            lastToggleTime[relayNum] = millis();
            
            // Store delta (represents device's running current)
            if (deviceStates[relayNum]) {
                // Device turned ON - store positive delta
                deviceCurrents[relayNum] = delta;
            } else {
                // Device turned OFF - delta should match what we stored
                // Keep the stored value for reference
            }
            
            Serial.printf("[Relay] CH%d Delta: %.2fA (now %s)\n", 
                         relayNum, delta, deviceStates[relayNum] ? "ON" : "OFF");
                         
            // Health warning if delta is 0 but device should have drawn current
            if (delta < minDeltaThreshold && deviceStates[relayNum]) {
                Serial.printf("[Relay] ⚠️ WARNING: CH%d reports 0A - check relay/device!\n", relayNum);
            }
        }
        
        return delta;
    }
    
    /**
     * Set relay to specific state with delta measurement
     * @param relayNum Relay number 1-15
     * @param on Desired state
     * @return Delta current measured during transition
     */
    float setRelayState(int relayNum, bool on) {
        if (relayNum < 1 || relayNum > 15) return 0.0f;
        
        // Only pulse if state is different
        if (deviceStates[relayNum] != on) {
            return pulseRelay(relayNum);
        }
        
        return 0.0f;  // No change needed
    }

    void setFan(bool on) {
        // Capture baseline if sensor attached
        float baseline = currentSensor ? getMainLineAmps() : 0.0f;
        
        if (on) {
            currentRegisterState |= (1 << FAN_BIT);
        } else {
            currentRegisterState &= ~(1 << FAN_BIT);
        }
        updateShiftRegisters(currentRegisterState);
        
        // Measure delta for fan
        if (currentSensor) {
            vTaskDelay(pdMS_TO_TICKS(SETTLE_TIME_MS));
            float final_amps = getMainLineAmps();
            float delta = abs(final_amps - baseline);
            deviceCurrents[FAN_BIT] = (delta > minDeltaThreshold) ? delta : 0.0f;
            deviceStates[FAN_BIT] = on;
            Serial.printf("[Relay] Fan Delta: %.2fA\n", deviceCurrents[FAN_BIT]);
        }
    }

    void emergencyShutdown() {
        currentRegisterState = 0x0000;
        updateShiftRegisters(0x0000);
        digitalWrite(OE_PIN, HIGH);
        
        // Reset all state tracking
        for (int i = 0; i < 16; i++) {
            deviceStates[i] = false;
        }
        
        Serial.println("[Relay] Emergency shutdown activated");
    }

    // === Getters ===
    
    uint16_t getState() const { return currentRegisterState; }
    
    /**
     * Get the stored delta current for a specific relay channel
     * This represents the device's running current (captured during last toggle)
     */
    float getDeviceAmps(int relayNum) const {
        if (relayNum < 0 || relayNum > 15) return 0.0f;
        return deviceCurrents[relayNum];
    }
    
    /**
     * Get device ON/OFF state
     */
    bool getDeviceState(int relayNum) const {
        if (relayNum < 0 || relayNum > 15) return false;
        return deviceStates[relayNum];
    }
    
    /**
     * Get total current (live reading - BLOCKING, ~3ms)
     */
    float getTotalAmps() const {
        if (!currentSensor) return 0.0f;
        return const_cast<RelayController*>(this)->getMainLineAmps();
    }
    
    /**
     * Get cached total current (NON-BLOCKING for UI sync)
     * Use this in WebSocket sync to avoid blocking
     */
    float getCachedTotalAmps() const {
        if (!currentSensor) return 0.0f;
        return currentSensor->getCachedAmps();
    }
    
    /**
     * Check if a device has valid current reading (health check)
     * Returns false if device is ON but drawing 0A
     */
    bool isDeviceHealthy(int relayNum) const {
        if (relayNum < 0 || relayNum > 15) return true;
        if (!deviceStates[relayNum]) return true;  // OFF is always "healthy"
        return deviceCurrents[relayNum] >= minDeltaThreshold;
    }
    
    /**
     * Set device state externally (for sync with DeviceManager)
     */
    void syncDeviceState(int relayNum, bool state) {
        if (relayNum >= 0 && relayNum < 16) {
            deviceStates[relayNum] = state;
        }
    }

private:
    inline void updateShiftRegisters(uint16_t data) {
        digitalWrite(LATCH_PIN, LOW);
        shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (data >> 8));
        shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, (data & 0xFF));
        digitalWrite(LATCH_PIN, HIGH);
    }
    
    /**
     * Helper to get current from sensor (avoids include dependency)
     * Implementation must be after CurrentSensorManager is defined
     */
    float getMainLineAmps();
};

// Include CurrentSensorManager for the implementation
#include "CurrentSensorManager.h"

// Implementation of getMainLineAmps (must be after CurrentSensorManager is defined)
inline float RelayController::getMainLineAmps() {
    if (!currentSensor) return 0.0f;
    return currentSensor->getMainLineAmps();
}

#endif