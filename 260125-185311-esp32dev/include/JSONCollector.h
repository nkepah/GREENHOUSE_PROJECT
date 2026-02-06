#ifndef JSON_COLLECTOR_H
#define JSON_COLLECTOR_H

#include <ArduinoJson.h>
#include <map>
#include <deque>
#include <memory>

/**
 * JSONCollector - Batches UI commands and sensor data for efficient transmission
 * 
 * Features:
 * - Command queue (UI events, device toggles, config updates)
 * - Sensor data batching (telemetry, sensor readings)
 * - Automatic flush on threshold or timeout
 * - Deduplication (last command for same device wins)
 * - Priority levels (critical vs normal)
 */

struct JSONCommand {
    enum Priority { CRITICAL = 0, NORMAL = 1, BACKGROUND = 2 };
    
    String type;           // "toggle", "config", "move_device", etc.
    String deviceId;       // Which device
    JsonDocument payload;  // Command data
    Priority priority;
    unsigned long timestamp;
    bool needsReply;       // Should UI wait for response?
    
    JSONCommand(const String& t, const String& id, Priority p = NORMAL, bool reply = false)
        : type(t), deviceId(id), priority(p), needsReply(reply), timestamp(millis()) {}
};

class JSONCollector {
private:
    std::deque<JSONCommand> commandQueue;
    JsonDocument currentTelemetry;  // Current sensor state to include
    
    // Configuration
    static constexpr size_t MAX_QUEUE_SIZE = 50;
    static constexpr unsigned long BATCH_TIMEOUT_MS = 250;  // Flush every 250ms max
    static constexpr size_t BATCH_SIZE_THRESHOLD = 10;      // Flush at 10 commands
    
    unsigned long lastFlushTime = 0;
    
public:
    JSONCollector();
    
    /**
     * Queue a command from the UI
     * Returns: true if queued successfully, false if queue full
     */
    bool queueCommand(const String& type, const String& deviceId, 
                      const JsonDocument& payload = JsonDocument(),
                      JSONCommand::Priority priority = JSONCommand::NORMAL,
                      bool needsReply = false);
    
    /**
     * Queue a device toggle (most common operation)
     */
    bool queueToggle(const String& deviceId, bool newState, bool needsReply = true);
    
    /**
     * Queue a device movement/repositioning
     */
    bool queueMove(const String& deviceId, int x, int y, bool mobile = false);
    
    /**
     * Update current telemetry snapshot (temp, amps, etc)
     */
    void updateTelemetry(const JsonDocument& telemetry);
    
    /**
     * Check if batch should be flushed
     */
    bool shouldFlush() const;
    
    /**
     * Build and return the batch JSON
     * Clears the queue after building
     */
    String buildBatch();
    
    /**
     * Get queue size
     */
    size_t getQueueSize() const { return commandQueue.size(); }
    
    /**
     * Force clear the queue (on disconnect, etc)
     */
    void clear();
    
    /**
     * Get telemetry data
     */
    const JsonDocument& getTelemetry() const { return currentTelemetry; }
};

#endif
