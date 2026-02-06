#ifndef CURRENT_SENSOR_MANAGER_H
#define CURRENT_SENSOR_MANAGER_H

#include <Arduino.h>

/**
 * CurrentSensorManager - Delta-based single-clamp current sensing
 * 
 * Hardware: SCT-013-100 (100A max, 50mA output) Current Transformer
 * Configuration: Wire wrapped 3 TIMES through the clamp for better sensitivity
 * 
 * SCT-013-100 Specs:
 * - Turn ratio: 2000:1 (100A input → 50mA output)
 * - With 3 wraps: Effective ratio becomes 2000:3 = 666.67:1
 * - This gives 3x sensitivity for better low-current detection!
 * 
 * Circuit Setup:
 * - Burden resistor: 33Ω (gives ~1.65V at 100A with standard ratio)
 * - Bias circuit: Two 10kΩ resistors to create 1.65V mid-rail bias
 * - Output connected to ESP32 ADC pin (GPIO34 recommended - ADC1)
 * 
 * Algorithm:
 * 1. Baseline: Measure current BEFORE relay toggle
 * 2. Action: Toggle the relay
 * 3. Settle: Wait ~60ms for inrush to stabilize (1-3 AC cycles)
 * 4. Final: Measure current AFTER
 * 5. Delta: |Final - Baseline| = Device's running current
 * 
 * Benefits of 3 wraps:
 * - 3x sensitivity for small loads (blowers, actuators drawing 0.1-0.5A)
 * - Better noise immunity
 * - Still handles up to ~33A actual current before saturation
 */

class CurrentSensorManager {
private:
    // SCT-013-100 Configuration
    static constexpr float CT_RATIO = 2000.0f;           // Turns ratio (100A:50mA)
    static constexpr int WIRE_WRAPS = 3;                  // Number of times wire passes through CT
    static constexpr float BURDEN_RESISTOR = 33.0f;       // Burden resistor in Ohms
    
    // Effective calibration factor
    // With 3 wraps, sensitivity is 3x, so we divide by 3 to get actual current
    static constexpr float WRAP_FACTOR = 1.0f / WIRE_WRAPS;  // = 0.333...
    
    // ESP32 ADC Configuration
    static constexpr float ADC_VREF = 3.3f;
    static constexpr int ADC_RESOLUTION = 4095;
    static constexpr float ADC_MIDPOINT_V = 1.65f;        // Bias voltage (VCC/2)
    
    // Calculate amps per ADC unit
    // V_out = (I_primary / CT_RATIO) * BURDEN_RESISTOR * WIRE_WRAPS
    // I_primary = V_out * CT_RATIO / (BURDEN_RESISTOR * WIRE_WRAPS)
    static constexpr float AMPS_PER_VOLT = CT_RATIO / (BURDEN_RESISTOR * WIRE_WRAPS);
    // = 2000 / (33 * 3) = 20.20 A/V
    
    // Sampling configuration - optimized for speed
    static constexpr uint8_t SAMPLES_PER_CYCLE = 40;      // Reduced for speed
    static constexpr uint8_t SAMPLE_DELAY_US = 80;        // 80µs between samples
    static constexpr uint8_t NUM_CYCLES = 1;              // Single AC cycle
    
    // Fast continuous reading cache
    volatile float cachedAmps = 0.0f;
    volatile unsigned long lastReadTime = 0;
    
    // === NOISE COMPENSATION ===
    // === NOISE COMPENSATION ===
    // Raw sensor noise floor: ~0.5A equivalent reading before any processing
    // The 3x wrap BOOSTS the signal by 3x to rise above this noise.
    // Example: 0.5A device → appears as 1.5A raw → divided by 3 → 0.5A reported
    // Meanwhile noise can reach ~0.7A raw → divided by 3 → ~0.23A noise floor
    // OBSERVED: Max 0.23A noise with no device plugged in, threshold set just above
    static constexpr float RAW_NOISE_AMPS = 0.7f;         // Observed raw sensor noise (~0.7A with no load)
    static constexpr float EFFECTIVE_NOISE = RAW_NOISE_AMPS / WIRE_WRAPS;  // ~0.23A after wrap division
    static constexpr float MIN_CURRENT_THRESHOLD = 0.25f; // Detection threshold (above 0.23A observed noise)
    static constexpr float NOISE_FLOOR_V = 0.01f;         // ADC noise floor in volts
    
    int mainSensorPin = -1;
    float calibrationOffset = 0.0f;   // Zero-point offset (voltage)
    float calibrationFactor = 1.0f;   // User calibration multiplier
    float measuredNoiseFloor = 0.0f;  // Actual measured noise floor during calibration
    bool calibrated = false;

public:
    CurrentSensorManager() = default;
    
    /**
     * Initialize the sensor
     * @param pin GPIO pin connected to CT output (must be ADC1 - GPIO 32-39)
     */
    void begin(int pin = 34) {
        mainSensorPin = pin;
        pinMode(mainSensorPin, INPUT);
        
        // Configure ADC for better accuracy
        analogReadResolution(12);
        analogSetAttenuation(ADC_11db);
        
        Serial.println(F("[Current] SCT-013-100 CT Initialized"));
        Serial.printf("[Current] Pin: %d, Wraps: %d, Range: 0-%.1fA\n", 
            mainSensorPin, WIRE_WRAPS, 100.0f / WIRE_WRAPS);
        
        // Auto-calibrate zero point
        calibrate();
    }
    
    /**
     * Calibrate zero-current offset
     * Call this when you KNOW there's no load on the line
     */
    void calibrate() {
        if (mainSensorPin < 0) return;
        
        Serial.println(F("[Current] Calibrating..."));
        
        // Take samples to find zero point
        float sumVoltage = 0.0f;
        int validSamples = 0;
        
        for (int cycle = 0; cycle < 3; cycle++) {  // Reduced to 3 cycles
            for (int i = 0; i < SAMPLES_PER_CYCLE; i++) {
                int raw = analogRead(mainSensorPin);
                float voltage = (raw * ADC_VREF) / ADC_RESOLUTION;
                sumVoltage += voltage;
                validSamples++;
                delayMicroseconds(SAMPLE_DELAY_US);
            }
        }
        
        float avgVoltage = sumVoltage / validSamples;
        calibrationOffset = avgVoltage - ADC_MIDPOINT_V;
        
        // Measure actual noise floor
        float noiseSquareSum = 0.0f;
        const uint8_t noiseSamples = 100;  // Reduced from 200
        for (int i = 0; i < noiseSamples; i++) {
            int raw = analogRead(mainSensorPin);
            float voltage = (raw * ADC_VREF) / ADC_RESOLUTION;
            float vCentered = voltage - ADC_MIDPOINT_V - calibrationOffset;
            float instantCurrent = abs(vCentered * AMPS_PER_VOLT);
            noiseSquareSum += instantCurrent * instantCurrent;
            delayMicroseconds(SAMPLE_DELAY_US * 3);
        }
        measuredNoiseFloor = sqrt(noiseSquareSum / noiseSamples);
        measuredNoiseFloor = constrain(measuredNoiseFloor, 0.05f, 0.5f);
        
        calibrated = true;
        
        Serial.printf("[Current] Zero: %.3fV, Noise: %.3fA\n", avgVoltage, measuredNoiseFloor);
    }
    
    /**
     * Set manual calibration factor (for fine-tuning against known load)
     * @param factor Multiplier (e.g., 1.05 if reading 5% low)
     */
    void setCalibrationFactor(float factor) {
        calibrationFactor = factor;
        Serial.printf("[Current] Calibration factor set to: %.3f\n", factor);
    }
    
    /**
     * Get RMS current reading
     * Uses true RMS calculation over multiple AC cycles
     * @return Current in Amps (actual, accounting for 3 wraps)
     */
    float getMainLineAmps() {
        if (mainSensorPin < 0) return 0.0f;
        
        float sumSquares = 0.0f;
        const int totalSamples = SAMPLES_PER_CYCLE * NUM_CYCLES;
        const float voltScale = ADC_VREF / ADC_RESOLUTION;
        
        for (int i = 0; i < totalSamples; i++) {
            int raw = analogRead(mainSensorPin);
            float vCentered = (raw * voltScale) - ADC_MIDPOINT_V - calibrationOffset;
            float instantCurrent = vCentered * AMPS_PER_VOLT;
            sumSquares += instantCurrent * instantCurrent;
            delayMicroseconds(SAMPLE_DELAY_US);
        }
        
        // RMS = sqrt(sum of squares / n)
        float rms = sqrt(sumSquares / totalSamples);
        
        // Apply user calibration factor
        rms *= calibrationFactor;
        
        // Subtract measured noise floor (but don't go negative)
        // This compensates for the persistent ~0.5A raw noise
        float noiseCompensated = rms - measuredNoiseFloor;
        if (noiseCompensated < 0.0f) noiseCompensated = 0.0f;
        
        // Apply minimum threshold - anything below is considered noise/zero
        if (noiseCompensated < MIN_CURRENT_THRESHOLD) {
            noiseCompensated = 0.0f;
        }
        
        return noiseCompensated;
    }
    
    /**
     * Get smoothed current reading (faster)
     */
    float getSmoothedAmps() {
        float sum = 0.0f;
        constexpr uint8_t readings = 3;  // Reduced from 5
        
        for (int i = 0; i < readings; i++) {
            sum += getMainLineAmps();
            vTaskDelay(pdMS_TO_TICKS(30));  // 30ms between readings
        }
        return sum / readings;
    }
    
    /**
     * Get peak current (useful for inrush detection)
     */
    float getPeakAmps() {
        if (mainSensorPin < 0) return 0.0f;
        
        float maxCurrent = 0.0f;
        const int totalSamples = SAMPLES_PER_CYCLE * NUM_CYCLES;
        const float voltScale = ADC_VREF / ADC_RESOLUTION;
        
        for (int i = 0; i < totalSamples; i++) {
            int raw = analogRead(mainSensorPin);
            float vCentered = (raw * voltScale) - ADC_MIDPOINT_V - calibrationOffset;
            float instantCurrent = fabsf(vCentered * AMPS_PER_VOLT);
            
            if (instantCurrent > maxCurrent) maxCurrent = instantCurrent;
            delayMicroseconds(SAMPLE_DELAY_US);
        }
        
        return maxCurrent * calibrationFactor;
    }
    
    /**
     * Read raw ADC value for debugging
     */
    int getRawADC() {
        if (mainSensorPin < 0) return 0;
        return analogRead(mainSensorPin);
    }
    
    /**
     * Get voltage reading for debugging
     */
    float getVoltage() {
        return (getRawADC() / (float)ADC_RESOLUTION) * ADC_VREF;
    }
    
    /**
     * Get centered voltage (after removing bias)
     */
    float getCenteredVoltage() {
        return getVoltage() - ADC_MIDPOINT_V - calibrationOffset;
    }
    
    /**
     * Get RAW RMS current (without noise compensation)
     * Useful for diagnostics and seeing actual sensor output
     */
    float getRawAmps() {
        if (mainSensorPin < 0) return 0.0f;
        
        float sumSquares = 0.0f;
        int totalSamples = SAMPLES_PER_CYCLE * NUM_CYCLES;
        
        for (int i = 0; i < totalSamples; i++) {
            int raw = analogRead(mainSensorPin);
            float voltage = (raw / (float)ADC_RESOLUTION) * ADC_VREF;
            float vCentered = voltage - ADC_MIDPOINT_V - calibrationOffset;
            float instantCurrent = vCentered * AMPS_PER_VOLT;
            sumSquares += instantCurrent * instantCurrent;
            delayMicroseconds(SAMPLE_DELAY_US);
        }
        
        return sqrt(sumSquares / totalSamples) * calibrationFactor;
    }
    
    // === Getters ===
    bool isCalibrated() const { return calibrated; }
    int getPin() const { return mainSensorPin; }
    float getCalibrationOffset() const { return calibrationOffset; }
    float getCalibrationFactor() const { return calibrationFactor; }
    float getNoiseFloor() const { return measuredNoiseFloor; }
    int getWireWraps() const { return WIRE_WRAPS; }
    float getMaxCurrent() const { return 100.0f / WIRE_WRAPS; }  // ~33A with 3 wraps
    float getMinDetectable() const { return MIN_CURRENT_THRESHOLD; }
    float getEffectiveNoise() const { return EFFECTIVE_NOISE; }
    
    /**
     * Get cached amps (instant return, no sampling delay)
     * Used by UI for fast display updates
     */
    float getCachedAmps() const { 
        return cachedAmps; 
    }
    
    /**
     * Get age of cached reading in milliseconds
     */
    unsigned long getCacheAge() const {
        return millis() - lastReadTime;
    }
    
    /**
     * Fast update for continuous monitoring (called by high-priority task)
     * Takes a quick reading and updates the cache
     */
    void updateContinuousReading() {
        if (mainSensorPin < 0) return;
        
        // Ultra-fast RMS: single half-cycle (~10ms at 50Hz)
        float sumSquares = 0.0f;
        const int fastSamples = 25;  // 25 samples over ~5ms
        
        for (int i = 0; i < fastSamples; i++) {
            int raw = analogRead(mainSensorPin);
            float voltage = (raw / (float)ADC_RESOLUTION) * ADC_VREF;
            float vCentered = voltage - ADC_MIDPOINT_V - calibrationOffset;
            float instantCurrent = vCentered * AMPS_PER_VOLT;
            sumSquares += instantCurrent * instantCurrent;
            delayMicroseconds(200);  // ~5ms total
        }
        
        float rms = sqrt(sumSquares / fastSamples) * calibrationFactor;
        
        // Noise compensation
        float noiseCompensated = rms - measuredNoiseFloor;
        if (noiseCompensated < 0.0f) noiseCompensated = 0.0f;
        if (noiseCompensated < MIN_CURRENT_THRESHOLD) noiseCompensated = 0.0f;
        
        cachedAmps = noiseCompensated;
        lastReadTime = millis();
    }
};

#endif // CURRENT_SENSOR_MANAGER_H
