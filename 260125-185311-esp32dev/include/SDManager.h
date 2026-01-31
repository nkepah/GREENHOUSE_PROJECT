#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <FS.h>

class SDManager {
public:
    SDManager();
    
    void begin();
    bool isInitialized();
    bool fileExists(const String& path);
    String readFile(const String& path);
    void writeFile(const String& path, const String& content);
    void deleteFile(const String& path);
    
private:
    bool initialized;
};

#endif
