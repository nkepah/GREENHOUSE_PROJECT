#include "JSONCollector.h"

JSONCollector::JSONCollector() {
    lastFlushTime = millis();
}

bool JSONCollector::queueCommand(const String& type, const String& deviceId, 
                                  const JsonDocument& payload,
                                  JSONCommand::Priority priority,
                                  bool needsReply) {
    if (commandQueue.size() >= MAX_QUEUE_SIZE) {
        Serial.printf("[JSONCollector] Queue full! Dropping %s command for device %s\n", 
                     type.c_str(), deviceId.c_str());
        return false;
    }
    
    // Deduplication: If same device+type exists, replace it (unless CRITICAL)
    if (priority != JSONCommand::CRITICAL) {
        for (auto& cmd : commandQueue) {
            if (cmd.deviceId == deviceId && cmd.type == type) {
                // Replace the old command
                cmd.payload.clear();
                cmd.payload = payload;
                cmd.needsReply = needsReply;
                cmd.timestamp = millis();
                Serial.printf("[JSONCollector] Deduped %s for %s (queue size: %u)\n", 
                             type.c_str(), deviceId.c_str(), (unsigned)commandQueue.size());
                return true;
            }
        }
    }
    
    JSONCommand cmd(type, deviceId, priority, needsReply);
    cmd.payload = payload;
    
    commandQueue.push_back(cmd);
    Serial.printf("[JSONCollector] Queued %s for %s (queue size: %u)\n", 
                 type.c_str(), deviceId.c_str(), (unsigned)commandQueue.size());
    
    return true;
}

bool JSONCollector::queueToggle(const String& deviceId, bool newState, bool needsReply) {
    JsonDocument payload;
    payload["state"] = newState;
    return queueCommand("toggle", deviceId, payload, JSONCommand::CRITICAL, needsReply);
}

bool JSONCollector::queueMove(const String& deviceId, int x, int y, bool mobile) {
    JsonDocument payload;
    payload["x"] = x;
    payload["y"] = y;
    if (mobile) {
        payload["x_mobile"] = x;
        payload["y_mobile"] = y;
    }
    // Move is background priority - can be deduped if new move comes in quickly
    return queueCommand("move_device", deviceId, payload, JSONCommand::BACKGROUND, false);
}

void JSONCollector::updateTelemetry(const JsonDocument& telemetry) {
    currentTelemetry = telemetry;
}

bool JSONCollector::shouldFlush() const {
    unsigned long now = millis();
    unsigned long timeSinceFlush = now - lastFlushTime;
    
    // Always flush if queue has items and timeout exceeded
    if (!commandQueue.empty() && timeSinceFlush >= BATCH_TIMEOUT_MS) {
        return true;
    }
    
    // Flush if queue is getting full
    if (commandQueue.size() >= BATCH_SIZE_THRESHOLD) {
        return true;
    }
    
    return false;
}

String JSONCollector::buildBatch() {
    unsigned long now = millis();
    JsonDocument batch;
    
    // Add metadata
    batch["type"] = "batch_command";
    batch["timestamp"] = now;
    batch["count"] = commandQueue.size();
    
    // Add current telemetry snapshot
    if (!currentTelemetry.isNull()) {
        batch["telemetry"] = currentTelemetry.as<JsonObject>();
    }
    
    // Add all commands (naturally sorted: CRITICAL = 0, NORMAL = 1, BACKGROUND = 2)
    JsonArray commands = batch["commands"].to<JsonArray>();
    
    // First pass: Add CRITICAL commands
    for (auto& cmd : commandQueue) {
        if (cmd.priority == JSONCommand::CRITICAL) {
            JsonObject cmdObj = commands.add<JsonObject>();
            cmdObj["type"] = cmd.type;
            cmdObj["id"] = cmd.deviceId;
            cmdObj["ts"] = cmd.timestamp;
            cmdObj["needsReply"] = cmd.needsReply;
            if (!cmd.payload.isNull()) {
                cmdObj["payload"] = cmd.payload.as<JsonObject>();
            }
        }
    }
    
    // Second pass: Add NORMAL commands
    for (auto& cmd : commandQueue) {
        if (cmd.priority == JSONCommand::NORMAL) {
            JsonObject cmdObj = commands.add<JsonObject>();
            cmdObj["type"] = cmd.type;
            cmdObj["id"] = cmd.deviceId;
            cmdObj["ts"] = cmd.timestamp;
            cmdObj["needsReply"] = cmd.needsReply;
            if (!cmd.payload.isNull()) {
                cmdObj["payload"] = cmd.payload.as<JsonObject>();
            }
        }
    }
    
    // Third pass: Add BACKGROUND commands
    for (auto& cmd : commandQueue) {
        if (cmd.priority == JSONCommand::BACKGROUND) {
            JsonObject cmdObj = commands.add<JsonObject>();
            cmdObj["type"] = cmd.type;
            cmdObj["id"] = cmd.deviceId;
            cmdObj["ts"] = cmd.timestamp;
            cmdObj["needsReply"] = cmd.needsReply;
            if (!cmd.payload.isNull()) {
                cmdObj["payload"] = cmd.payload.as<JsonObject>();
            }
        }
    }
    
    // Build JSON string
    String json;
    serializeJson(batch, json);
    
    // Clear queue
    commandQueue.clear();
    lastFlushTime = now;
    
    Serial.printf("[JSONCollector] Flushed batch: %u commands, %u bytes\n", 
                 commands.size(), (unsigned)json.length());
    
    return json;
}

void JSONCollector::clear() {
    commandQueue.clear();
    currentTelemetry.clear();
    lastFlushTime = millis();
}
