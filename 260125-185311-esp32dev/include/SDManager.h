#ifndef SDMANAGER_H
#define SDMANAGER_H

#include <SD.h>
#include <SPI.h>

class SDManager {
private:
    // SD Card SPI Pins (using free GPIOs)
    static constexpr int SD_CS = 15;      // Chip Select
    static constexpr int SD_MOSI = 23;    // Master Out Slave In
    static constexpr int SD_MISO = 19;    // Master In Slave Out
    static constexpr int SD_SCK = 18;     // Serial Clock
    
    bool isInitialized = false;

public:
    SDManager() = default;

    bool begin() {
        Serial.println(F("[SD] Initializing..."));
        
        // Initialize SPI with custom pins
        SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
        
        // Try to mount SD card with 3 attempts
        for(uint8_t attempt = 1; attempt <= 3; attempt++) {
            if(SD.begin(SD_CS)) {
                isInitialized = true;
                Serial.printf("[SD] Mounted - Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
                createDirectories();
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        
        Serial.println(F("[SD] Mount failed"));
        return false;
    }

    void createDirectories() {
        if(!isInitialized) return;
        
        const char* dirs[] = {"/logs", "/images", "/backups", "/data"};
        for(const char* dir : dirs) {
            if(!SD.exists(dir)) {
                if(SD.mkdir(dir)) {
                    Serial.printf("[SD] Created directory: %s\n", dir);
                }
            }
        }
    }

    bool isAvailable() const {
        return isInitialized;
    }

    void logData(const String& filename, const String& data) {
        if(!isInitialized) return;
        
        File file = SD.open(filename, FILE_APPEND);
        if(file) {
            file.println(data);
            file.close();
        } else {
            Serial.printf("[SD] Failed to open %s for writing\n", filename.c_str());
        }
    }

    String readFile(const String& path) {
        if(!isInitialized) return "";
        
        File file = SD.open(path);
        if(!file) return "";
        
        String content;
        while(file.available()) {
            content += (char)file.read();
        }
        file.close();
        return content;
    }

    bool fileExists(const String& path) {
        return isInitialized && SD.exists(path);
    }

    void listDirectory(const char* dirname, uint8_t levels = 1) {
        if(!isInitialized) return;
        
        Serial.printf("[SD] Listing directory: %s\n", dirname);
        File root = SD.open(dirname);
        if(!root || !root.isDirectory()) {
            Serial.println("[SD] Not a directory");
            return;
        }

        File file = root.openNextFile();
        while(file) {
            if(file.isDirectory()) {
                Serial.printf("  DIR : %s\n", file.name());
                if(levels) {
                    listDirectory(file.path(), levels - 1);
                }
            } else {
                Serial.printf("  FILE: %s\tSIZE: %d\n", file.name(), file.size());
            }
            file = root.openNextFile();
        }
    }

    uint64_t getTotalSpace() {
        return isInitialized ? SD.totalBytes() / (1024 * 1024) : 0;
    }

    uint64_t getUsedSpace() {
        return isInitialized ? SD.usedBytes() / (1024 * 1024) : 0;
    }
};

#endif
