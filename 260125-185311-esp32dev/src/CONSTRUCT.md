# Greenhouse OS - Master Project Specs

## 🔌 Hardware Architecture (ESP32)
- **Safety OE (Pin 33):** Must have 10k Pull-up to 3.3V. Must be HIGH during boot/OTA.
- **Latch RCLK (Pin 12):** Must have 10k Pull-down to GND.
- **Data SERIN (Pin 13)** | **Clock SRCLK (Pin 14)**
- **Shift Registers:** 2x 74HC595 (Daisy-chained, 16-bit word).

## 🗺️ Relay Mapping (Relay # -> SR Bit)
- R1:14, R2:2, R3:1, R4:3, R5:5, R6:6, R7:4, R8:0, R9:9, R10:11, R11:12, R12:13, R13:10, R14:8, R15:15
- **MOSFET Fan:** Dedicated to SR Bit 7.

## 🛡️ Safety & Logic Rules
1. **Latching Pulse:** Emylo relays require exactly 100ms HIGH pulse on the SR bit.
2. **Anti-Brick OTA:** Use `esp_ota_get_state_partition` and rollback logic. 
3. **FreeRTOS:** All hardware pulsing must happen in a dedicated task on Core 0.
4. **Digital Twin:** UI icons use JSON coordinates stored in LittleFS.

## 📅 Scheduler Logic
- Weather-aware (fetch via WiFi for frost protection).
- Seasonal Calendars (Disable heat in summer).
- Fan Offset: Fans run X mins after heater turns off.