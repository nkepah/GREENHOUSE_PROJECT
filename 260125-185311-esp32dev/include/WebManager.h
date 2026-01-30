#ifndef WEBMANAGER_H
#define WEBMANAGER_H

#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <LittleFS.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "SDManager.h"

// External function defined in main.cpp
void handleSocketData(AsyncWebSocketClient *client, uint8_t *data);
void handleSocketConnect(AsyncWebSocketClient *client);

class WebManager {
private:
    AsyncWebServer server;
    SDManager* sdCard = nullptr;
    
public:
    AsyncWebSocket ws;  // Public for direct access to count(), cleanupClients()
    
    explicit WebManager(int port) : server(port), ws("/ws") {}

    void begin(SDManager& sd) {
        sdCard = &sd;
        
        // Captive Portal redirect helper
        auto captiveRedirect = [](AsyncWebServerRequest *r) {
            r->redirect(F("http://192.168.4.1/"));
        };

        // Captive Portal detection endpoints (PROGMEM saves ~100 bytes)
        static const char* const captiveEndpoints[] PROGMEM = {
            "/generate_204", "/gen_204", "/ncsi.txt", 
            "/hotspot-detect.html", "/connectivity-check.html",
            "/mobile/status.php", "/success.html"
        };
        
        for(uint8_t i = 0; i < 7; i++) {
            server.on(captiveEndpoints[i], HTTP_GET, captiveRedirect);
        }

        // Root handler: redirect to Pi if proxyConnected
        server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
            extern volatile bool proxyConnected;
            extern char piIp[40];
            if (proxyConnected && strlen(piIp) > 0) {
                // Redirect to Pi - use port 80 (Nginx reverse proxy) if available, fallback to 3000
                String piUrl = String("http://") + piIp + "/";
                request->redirect(piUrl);
                return;
            }
            // Pi is down or not connected - serve local ESP32 UI
            if(LittleFS.exists("/index.html.gz")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/index.html.gz", F("text/html"));
                response->addHeader(F("Content-Encoding"), F("gzip"));
                response->addHeader(F("Cache-Control"), F("no-store"));
                request->send(response);
            } else {
                request->send(LittleFS, "/index.html", F("text/html"));
            }
        });
        
        // alerts.html with GZIP support
        server.on("/alerts.html", HTTP_GET, [](AsyncWebServerRequest *request) {
            if(LittleFS.exists("/alerts.html.gz")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/alerts.html.gz", F("text/html"));
                response->addHeader(F("Content-Encoding"), F("gzip"));
                response->addHeader(F("Cache-Control"), F("no-store"));
                request->send(response);
            } else {
                request->send(LittleFS, "/alerts.html", F("text/html"));
            }
        });
        
        // routines.html with GZIP support
        server.on("/routines.html", HTTP_GET, [](AsyncWebServerRequest *request) {
            if(LittleFS.exists("/routines.html.gz")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/routines.html.gz", F("text/html"));
                response->addHeader(F("Content-Encoding"), F("gzip"));
                response->addHeader(F("Cache-Control"), F("no-store"));
                request->send(response);
            } else {
                request->send(LittleFS, "/routines.html", F("text/html"));
            }
        });
        
        // setup.html - Minimal setup page for direct ESP32 access
        server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
            if(LittleFS.exists("/setup.html.gz")) {
                AsyncWebServerResponse *response = request->beginResponse(LittleFS, "/setup.html.gz", F("text/html"));
                response->addHeader(F("Content-Encoding"), F("gzip"));
                response->addHeader(F("Cache-Control"), F("no-store"));
                request->send(response);
            } else {
                request->send(LittleFS, "/setup.html", F("text/html"));
            }
        });

        // Serve large files from SD if available, fallback to LittleFS
        server.on("/icons.png", HTTP_GET, [this](AsyncWebServerRequest *request) {
            if(sdCard && sdCard->isAvailable() && sdCard->fileExists("/icons.png")) {
                request->send(SD, "/icons.png", F("image/png"));
            } else {
                request->send(LittleFS, "/icons.png", F("image/png"));
            }
        });

        // SD card info endpoint (for debugging)
        server.on("/sd", HTTP_GET, [this](AsyncWebServerRequest *request) {
            if(sdCard && sdCard->isAvailable()) {
                char buf[128];
                snprintf(buf, sizeof(buf), "<h1>SD Card Info</h1><pre>Total: %lluMB\nUsed: %lluMB</pre>",
                    sdCard->getTotalSpace(), sdCard->getUsedSpace());
                request->send(200, F("text/html"), buf);
            } else {
                request->send(503, F("text/plain"), F("SD card not available"));
            }
        });

        // Static files with longer cache control (1 week) - LittleFS first
        server.serveStatic("/", LittleFS, "/").setCacheControl("max-age=604800");

        // OTA Update handler
        server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
            bool shouldReboot = !Update.hasError();
            AsyncWebServerResponse *response = request->beginResponse(200, F("text/plain"), 
                shouldReboot ? F("OK") : F("FAIL"));
            response->addHeader(F("Connection"), F("close"));
            response->addHeader(F("Access-Control-Allow-Origin"), F("*"));
            request->send(response);
            if(shouldReboot) {
                Serial.println(F("[OTA] Update successful, rebooting..."));
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP.restart();
            }
        }, [](AsyncWebServerRequest *request, String filename, size_t index, 
              uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[OTA] Update Start: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    Update.printError(Serial);
                }
            }
            if (len && Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA] Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        });
        
        // OTA CLI endpoint for curl/wget uploads
        server.on("/ota", HTTP_POST, [](AsyncWebServerRequest *request) {
            bool shouldReboot = !Update.hasError();
            request->send(200, F("application/json"),
                shouldReboot ? F("{\"status\":\"success\",\"message\":\"Update complete, rebooting...\"}") 
                             : F("{\"status\":\"error\",\"message\":\"Update failed\"}"));
            if(shouldReboot) {
                Serial.println(F("[OTA-CLI] Update successful, rebooting..."));
                vTaskDelay(pdMS_TO_TICKS(1000));
                ESP.restart();
            }
        }, [](AsyncWebServerRequest *request, String filename, size_t index, 
              uint8_t *data, size_t len, bool final) {
            if (!index) {
                Serial.printf("[OTA-CLI] Update Start: %s (%u bytes)\n", filename.c_str(), request->contentLength());
                
                // Auto-detect filesystem vs firmware
                int updateType = U_FLASH;
                if (request->hasParam("type", true)) {
                    String type = request->getParam("type", true)->value();
                    if (type == "filesystem" || type == "fs" || type == "spiffs" || type == "littlefs") {
                        updateType = U_SPIFFS;
                    }
                } else if (filename.indexOf("littlefs") >= 0 || filename.indexOf("spiffs") >= 0) {
                    updateType = U_SPIFFS;
                }
                
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, updateType)) {
                    Update.printError(Serial);
                    return;
                }
            }
            if (len && Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[OTA-CLI] Update Success: %u bytes\n", index + len);
                } else {
                    Update.printError(Serial);
                }
            }
        });

        // /api/status - Device status endpoint for Pi polling
        server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
            extern float currentTemperature;
            extern CurrentSensorManager currentSensor;
            
            // Build JSON response
            DynamicJsonDocument doc(512);
            doc["online"] = true;
            doc["temp"] = currentTemperature;
            doc["amps"] = currentSensor.getMainLineAmps();  // Get current reading
            doc["humidity"] = 0;  // TODO: Add humidity when sensor is connected
            
            String json;
            serializeJson(doc, json);
            request->send(200, F("application/json"), json);
        });

        // 404 handler with smart redirect
        server.onNotFound([captiveRedirect](AsyncWebServerRequest *request) {
            if (request->client()->localIP() == IPAddress(192,168,4,1)) {
                captiveRedirect(request);
            } else {
                request->send(404, F("text/plain"), F("Not Found"));
            }
        });

        // WebSocket with minimal logging
        ws.onEvent([this](AsyncWebSocket *server, AsyncWebSocketClient *client, 
                          AwsEventType type, void *arg, uint8_t *data, size_t len) {
            switch(type) {
                case WS_EVT_CONNECT:
                    handleSocketConnect(client);
                    break;
                case WS_EVT_DISCONNECT:
                    break;
                case WS_EVT_DATA:
                    handleSocketData(client, data);
                    break;
                default:
                    break;
            }
        });

        server.addHandler(&ws);
        server.begin();
        Serial.println(F("[WEB] Server Started"));
    }

    void broadcastStatus(const String &json) { 
        ws.cleanupClients();
        if(ws.count() > 0) {
            ws.textAll(json); 
        }
    }
    
    // Overload for F() strings
    void broadcastStatus(const __FlashStringHelper* json) {
        ws.cleanupClients();
        if(ws.count() > 0) {
            ws.textAll(String(json));
        }
    }
    
    void cleanup() { 
        ws.cleanupClients(3);
    }
    
    AsyncWebServer* getServer() { return &server; }
};
#endif