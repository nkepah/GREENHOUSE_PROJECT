/**
 * OTA Event Logger
 * 
 * Tracks all OTA update events with:
 * - Date/time stamps
 * - Device information
 * - Firmware versions (before/after)
 * - Update status and results
 * - Error logs
 * 
 * Usage:
 * const otaLogger = require('./ota-logger');
 * otaLogger.logOTAEvent('success', 'ESP32-XXXX', '1.0.0', '1.1.0', 'Update completed');
 */

const fs = require('fs');
const path = require('path');

// OTA logs directory
const OTA_LOGS_DIR = path.join(__dirname, 'logs');
const OTA_LOG_FILE = path.join(OTA_LOGS_DIR, 'ota-events.json');
const MAX_LOGS = 1000; // Keep last 1000 OTA events in memory

// Create logs directory if it doesn't exist
if (!fs.existsSync(OTA_LOGS_DIR)) {
    fs.mkdirSync(OTA_LOGS_DIR, { recursive: true });
    console.log('[OTA Logger] Created logs directory');
}

// In-memory OTA event log (most recent first)
let otaEventLog = [];

/**
 * Load OTA events from disk
 */
function loadOTAEvents() {
    try {
        if (fs.existsSync(OTA_LOG_FILE)) {
            const data = fs.readFileSync(OTA_LOG_FILE, 'utf8');
            otaEventLog = JSON.parse(data);
            console.log(`[OTA Logger] Loaded ${otaEventLog.length} OTA events from disk`);
        }
    } catch (err) {
        console.error('[OTA Logger] Error loading OTA events:', err.message);
        otaEventLog = [];
    }
}

/**
 * Save OTA events to disk
 */
function saveOTAEvents() {
    try {
        // Keep only last MAX_LOGS events
        const eventsToSave = otaEventLog.slice(0, MAX_LOGS);
        fs.writeFileSync(OTA_LOG_FILE, JSON.stringify(eventsToSave, null, 2));
    } catch (err) {
        console.error('[OTA Logger] Error saving OTA events:', err.message);
    }
}

/**
 * Log OTA Event
 * 
 * @param {string} status - 'checking', 'downloading', 'updating', 'success', 'failed', 'retrying', 'no-update'
 * @param {string} deviceId - Device ID (e.g., 'ESP32-AABBCCDD')
 * @param {string} versionBefore - Firmware version before update
 * @param {string} versionAfter - Firmware version after update (null if no update)
 * @param {string} message - Status message or error details
 * @param {object} extraData - Additional data (optional)
 */
function logOTAEvent(status, deviceId, versionBefore, versionAfter, message = '', extraData = {}) {
    const event = {
        timestamp: new Date().toISOString(),
        unix_timestamp: Math.floor(Date.now() / 1000),
        status: status,
        device_id: deviceId,
        version_before: versionBefore,
        version_after: versionAfter || versionBefore,
        version_changed: versionAfter && versionAfter !== versionBefore,
        message: message,
        ...extraData
    };
    
    // Add to front of array (most recent first)
    otaEventLog.unshift(event);
    
    // Keep array size reasonable
    if (otaEventLog.length > MAX_LOGS) {
        otaEventLog = otaEventLog.slice(0, MAX_LOGS);
    }
    
    // Save to disk
    saveOTAEvents();
    
    // Log to console with color coding
    const statusSymbol = {
        'checking': '🔍',
        'downloading': '⬇️',
        'updating': '🔄',
        'success': '✅',
        'failed': '❌',
        'retrying': '🔁',
        'no-update': 'ℹ️'
    }[status] || '•';
    
    console.log(`[OTA] ${statusSymbol} ${status.toUpperCase()} - ${deviceId}: ${versionBefore} → ${versionAfter || versionBefore} (${message})`);
    
    return event;
}

/**
 * Get OTA events with optional filtering
 * 
 * @param {object} filters - Filter criteria
 *   - device_id: Filter by device
 *   - status: Filter by status
 *   - limit: Max results (default: 100)
 *   - offset: Pagination offset (default: 0)
 * @returns {object} { events: [], total: number }
 */
function getOTAEvents(filters = {}) {
    let filtered = [...otaEventLog];
    
    // Filter by device_id
    if (filters.device_id) {
        filtered = filtered.filter(e => e.device_id === filters.device_id);
    }
    
    // Filter by status
    if (filters.status) {
        const statuses = Array.isArray(filters.status) ? filters.status : [filters.status];
        filtered = filtered.filter(e => statuses.includes(e.status));
    }
    
    // Filter by date range if provided
    if (filters.start_date) {
        const startTime = new Date(filters.start_date).getTime();
        filtered = filtered.filter(e => new Date(e.timestamp).getTime() >= startTime);
    }
    if (filters.end_date) {
        const endTime = new Date(filters.end_date).getTime();
        filtered = filtered.filter(e => new Date(e.timestamp).getTime() <= endTime);
    }
    
    // Apply pagination
    const offset = filters.offset || 0;
    const limit = Math.min(filters.limit || 100, 500); // Max 500 at once
    
    return {
        events: filtered.slice(offset, offset + limit),
        total: filtered.length,
        offset: offset,
        limit: limit
    };
}

/**
 * Get OTA statistics
 * 
 * @returns {object} OTA statistics
 */
function getOTAStats() {
    const total = otaEventLog.length;
    const successful = otaEventLog.filter(e => e.status === 'success').length;
    const failed = otaEventLog.filter(e => e.status === 'failed').length;
    const retried = otaEventLog.filter(e => e.status === 'retrying').length;
    
    // Unique devices updated
    const devicesUpdated = new Set(
        otaEventLog
            .filter(e => e.version_changed)
            .map(e => e.device_id)
    ).size;
    
    // Success rate
    const attempts = successful + failed;
    const successRate = attempts > 0 ? Math.round((successful / attempts) * 100) : 0;
    
    // Most recent event
    const mostRecent = otaEventLog[0] || null;
    
    return {
        total_events: total,
        successful_updates: successful,
        failed_updates: failed,
        retry_attempts: retried,
        unique_devices_updated: devicesUpdated,
        success_rate: successRate,
        most_recent_event: mostRecent
    };
}

/**
 * Get OTA history for a specific device
 * 
 * @param {string} deviceId - Device ID
 * @param {number} limit - Max results (default: 50)
 * @returns {array} OTA events for device
 */
function getDeviceOTAHistory(deviceId, limit = 50) {
    return otaEventLog
        .filter(e => e.device_id === deviceId)
        .slice(0, limit);
}

/**
 * Clear old OTA logs (older than days)
 * 
 * @param {number} days - Clear logs older than this many days
 * @returns {number} Number of logs removed
 */
function clearOldLogs(days = 30) {
    const cutoffTime = Date.now() - (days * 24 * 60 * 60 * 1000);
    const originalLength = otaEventLog.length;
    
    otaEventLog = otaEventLog.filter(e => {
        return new Date(e.timestamp).getTime() > cutoffTime;
    });
    
    const removed = originalLength - otaEventLog.length;
    saveOTAEvents();
    
    console.log(`[OTA Logger] Cleared ${removed} logs older than ${days} days`);
    return removed;
}

/**
 * Export OTA logs for analysis
 * 
 * @param {string} format - 'json', 'csv'
 * @returns {string} Exported data
 */
function exportLogs(format = 'json') {
    if (format === 'csv') {
        // CSV format
        const headers = ['Timestamp', 'Device ID', 'Status', 'Version Before', 'Version After', 'Message'];
        const rows = otaEventLog.map(e => [
            e.timestamp,
            e.device_id,
            e.status,
            e.version_before,
            e.version_after,
            `"${e.message.replace(/"/g, '""')}"` // Escape quotes in CSV
        ]);
        
        return [headers.join(','), ...rows.map(r => r.join(','))].join('\n');
    } else {
        // JSON format
        return JSON.stringify(otaEventLog, null, 2);
    }
}

// Initialize - load existing logs on startup
loadOTAEvents();

// Export functions
module.exports = {
    logOTAEvent,
    getOTAEvents,
    getOTAStats,
    getDeviceOTAHistory,
    clearOldLogs,
    exportLogs
};
