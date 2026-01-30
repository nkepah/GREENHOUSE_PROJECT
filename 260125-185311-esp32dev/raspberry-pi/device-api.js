/**
 * Device Management API Endpoints for Raspberry Pi Server
 * Add these endpoints to /opt/greenhouse-proxy/server.js
 * 
 * Handles:
 * - ESP32 device handshake and registration
 * - Device-specific OTA firmware deployment
 * - Device type identification and routing
 * - OTA event logging and push updates
 */

const otaLogger = require('./ota-logger');

// Device registry (in-memory for now, could use database)
const registeredDevices = new Map();

// OTA push queue (for manual OTA triggers via API)
const otaPushQueue = new Map(); // device_id -> { version, timestamp }

/**
 * Device Handshake Endpoint
 * ESP32 calls this on first successful WiFi connection
 * POST /api/device/handshake
 */
function handleDeviceHandshake(ws, parsed) {
    const { device_id, device_type, ip_address, mac_address, rssi } = parsed;
    
    if (!device_id) {
        ws.send(JSON.stringify({ error: 'Missing device_id' }));
        return;
    }
    
    console.log(`[Device] Handshake from ${device_id}`);
    console.log(`  Type: ${device_type}, IP: ${ip_address}, RSSI: ${rssi}dBm`);
    
    // Store device info
    registeredDevices.set(device_id, {
        device_id,
        device_type,
        ip_address,
        mac_address,
        rssi,
        connected_at: new Date().toISOString(),
        last_seen: Date.now()
    });
    
    // Confirm handshake
    ws.send(JSON.stringify({
        type: 'handshake_ack',
        message: 'Handshake successful',
        server_time: Math.floor(Date.now() / 1000)
    }));
}

/**
 * Device Registration Endpoint
 * ESP32 sends complete device info after handshake
 * POST /api/device/register
 */
function handleDeviceRegister(ws, parsed) {
    const { device_id, device_type, ip_address, firmware_version } = parsed;
    
    if (!device_id) {
        ws.send(JSON.stringify({ error: 'Missing device_id' }));
        return;
    }
    
    console.log(`[Device] Registration: ${device_id}`);
    console.log(`  Firmware: ${firmware_version}, IP: ${ip_address}`);
    
    // Update device registry
    if (registeredDevices.has(device_id)) {
        const device = registeredDevices.get(device_id);
        device.firmware_version = firmware_version;
        device.last_seen = Date.now();
    } else {
        registeredDevices.set(device_id, {
            device_id,
            device_type,
            ip_address,
            firmware_version,
            connected_at: new Date().toISOString(),
            last_seen: Date.now()
        });
    }
    
    // Check if OTA update is needed
    const updateAvailable = checkForOTAUpdate(device_type, firmware_version);
    
    ws.send(JSON.stringify({
        type: 'registration_ack',
        message: 'Device registered',
        ota_available: updateAvailable,
        ota_url: updateAvailable ? `/api/device/ota/${device_id}` : null
    }));
    
    // Broadcast device status update
    broadcastDeviceStatus('device_registered', {
        device_id,
        device_type,
        ip_address
    });
}

/**
 * OTA Firmware Download Endpoint
 * ESP32 fetches device-specific firmware
 * GET /api/device/ota/:device_id
 */
function handleDeviceOTA(req, res, device_id) {
    console.log(`[OTA] Firmware request from ${device_id}`);
    
    const device = registeredDevices.get(device_id);
    if (!device) {
        console.warn(`[OTA] Unknown device: ${device_id}`);
        otaLogger.logOTAEvent('failed', device_id, 'unknown', 'unknown', 'Device not found in registry');
        res.status(404).json({ error: 'Device not found' });
        return;
    }
    
    const firmwarePath = getDeviceFirmwarePath(device.device_type, device.device_id);
    
    if (!fs.existsSync(firmwarePath)) {
        console.warn(`[OTA] Firmware not found: ${firmwarePath}`);
        otaLogger.logOTAEvent('failed', device_id, device.firmware_version, device.firmware_version, `Firmware not found at ${firmwarePath}`);
        res.status(404).json({ error: 'Firmware not available' });
        return;
    }
    
    const firmwareSize = fs.statSync(firmwarePath).size;
    console.log(`[OTA] Sending firmware to ${device_id} (${firmwareSize} bytes) from ${firmwarePath}`);
    
    // Log OTA start
    otaLogger.logOTAEvent('downloading', device_id, device.firmware_version, device.firmware_version, `Sending firmware (${firmwareSize} bytes)`);
    
    // Set headers for OTA flash
    res.setHeader('Content-Type', 'application/octet-stream');
    res.setHeader('Content-Length', firmwareSize);
    res.setHeader('X-Device-ID', device_id);
    res.setHeader('X-Device-Type', device.device_type);
    res.setHeader('X-Firmware-Size', firmwareSize);
    
    // Stream firmware file
    const fileStream = fs.createReadStream(firmwarePath);
    fileStream.pipe(res);
    
    fileStream.on('error', (err) => {
        console.error(`[OTA] Stream error: ${err.message}`);
        otaLogger.logOTAEvent('failed', device_id, device.firmware_version, device.firmware_version, `Stream error: ${err.message}`);
        if (!res.headersSent) {
            res.status(500).json({ error: 'Stream error' });
        }
    });
    
    fileStream.on('end', () => {
        console.log(`[OTA] Firmware sent to ${device_id}`);
        otaLogger.logOTAEvent('updating', device_id, device.firmware_version, device.firmware_version, `Firmware transfer complete, waiting for device update`);
        // Update device last_seen
        if (device) device.last_seen = Date.now();
    });
}

/**
 * Device Status Update Endpoint
 * ESP32 sends periodic status updates
 * POST /api/device/status
 */
function handleDeviceStatus(ws, parsed) {
    const { device_id, temperature, humidity, relay_states, uptime } = parsed;
    
    if (!device_id) return;
    
    const device = registeredDevices.get(device_id);
    if (device) {
        device.last_seen = Date.now();
        device.temperature = temperature;
        device.humidity = humidity;
        device.relay_states = relay_states;
        device.uptime = uptime;
    }
    
    // Could broadcast status to connected clients if needed
    // broadcastDeviceStatus('device_status_update', { device_id, ...parsed });
}

/**
 * Device Network Status Endpoint
 * ESP32 reports its network info (IP, hostname, RSSI, etc.) every 10 seconds
 * POST /api/device/network-status
 * 
 * Request payload:
 * {
 *   device_id: "ESP32-XXXX",
 *   device_name: "Greenhouse_Main",
 *   ip_address: "192.168.1.100",
 *   mac_address: "AA:BB:CC:DD:EE:FF",
 *   rssi: -65,
 *   hostname: "greenhouse-main",
 *   uptime_ms: 3600000,
 *   firmware_version: "2.1.0",
 *   free_heap: 50000,
 *   total_heap: 327680,
 *   dns_servers: ["192.168.1.1", "8.8.8.8"],
 *   gateway: "192.168.1.1",
 *   subnet_mask: "255.255.255.0"
 * }
 */
function handleDeviceNetworkStatus(ws, parsed) {
    const { device_id, ip_address, rssi, hostname, uptime_ms, firmware_version, free_heap, total_heap, heap_usage_percent, dns_servers, gateway, subnet_mask, device_name, mac_address, ota_status, ota_last_attempt } = parsed;
    
    if (!device_id) {
        ws.send(JSON.stringify({ error: 'Missing device_id' }));
        return;
    }
    
    const device = registeredDevices.get(device_id);
    if (device) {
        // Update network information
        device.last_seen = Date.now();
        device.ip_address = ip_address;
        device.rssi = rssi;
        device.hostname = hostname;
        device.device_name = device_name;
        device.uptime_ms = uptime_ms;
        device.firmware_version = firmware_version;
        device.free_heap = free_heap;
        device.total_heap = total_heap;
        device.heap_usage_percent = heap_usage_percent || (total_heap > 0 ? Math.round((free_heap / total_heap) * 100) : 0);
        device.dns_servers = dns_servers;
        device.gateway = gateway;
        device.subnet_mask = subnet_mask;
        device.mac_address = mac_address;
        device.network_status_updated = new Date().toISOString();
        device.ota_status = ota_status || 'idle';
        device.ota_last_attempt = ota_last_attempt || device.ota_last_attempt || 0;
        
        // Log OTA status changes
        if (device.previous_ota_status && device.previous_ota_status !== ota_status) {
            if (ota_status === 'success') {
                const oldVersion = device.previous_firmware_version || device.firmware_version;
                otaLogger.logOTAEvent('success', device_id, oldVersion, firmware_version, `Device reported successful OTA update`);
            } else if (ota_status === 'failed') {
                otaLogger.logOTAEvent('failed', device_id, device.firmware_version, device.firmware_version, `Device reported OTA update failure`);
            }
        }
        
        device.previous_ota_status = ota_status;
        device.previous_firmware_version = firmware_version;
        
        console.log(`[Network] ${device_id} (${hostname}) @ ${ip_address} - RSSI: ${rssi}dBm, Heap: ${device.heap_usage_percent}%, OTA: ${device.ota_status}`);
    } else {
        // Create new device entry if not registered yet
        registeredDevices.set(device_id, {
            device_id,
            device_name,
            ip_address,
            mac_address,
            rssi,
            hostname,
            uptime_ms,
            firmware_version,
            free_heap,
            total_heap,
            heap_usage_percent: heap_usage_percent || (total_heap > 0 ? Math.round((free_heap / total_heap) * 100) : 0),
            dns_servers,
            gateway,
            subnet_mask,
            connected_at: new Date().toISOString(),
            last_seen: Date.now(),
            network_status_updated: new Date().toISOString(),
            ota_status: ota_status || 'idle',
            ota_last_attempt: ota_last_attempt || 0
        });
        
        console.log(`[Network] New device discovered: ${device_id} (${hostname}) @ ${ip_address} - OTA: ${ota_status || 'idle'}`);
    }
    
    // Acknowledge the update
    ws.send(JSON.stringify({
        type: 'network_status_ack',
        message: 'Network status received',
        timestamp: Math.floor(Date.now() / 1000)
    }));
    
    // Broadcast device network status to web UI clients
    broadcastDeviceStatus('device_network_update', {
        device_id,
        hostname,
        ip_address,
        rssi,
        heap_usage_percent: device ? device.heap_usage_percent : 0,
        uptime_ms,
        timestamp: new Date().toISOString()
    });
}

/**
 * Get Device Firmware Path Based on Type
 * device_type: 1=Greenhouse, 2=ChickenCoop, 3=GrowBox, 4=HumidityStation, 255=Generic
 */
function getDeviceFirmwarePath(device_type, device_id) {
    const firmwareDir = '/opt/greenhouse-proxy/firmware';
    
    switch (device_type) {
        case 1:  // Greenhouse
            return `${firmwareDir}/greenhouse.bin`;
        case 2:  // Chicken Coop
            return `${firmwareDir}/chicken_coop.bin`;
        case 3:  // Grow Box
            return `${firmwareDir}/grow_box.bin`;
        case 4:  // Humidity Station
            return `${firmwareDir}/humidity_station.bin`;
        default: // Generic
            return `${firmwareDir}/generic.bin`;
    }
}

/**
 * Check for OTA Updates
 * Compare device firmware version with available version
 */
function checkForOTAUpdate(device_type, currentVersion) {
    // TODO: Implement version checking logic
    // For now, always return false (no updates needed)
    // In production, compare against latest firmware versions in database
    return false;
}

/**
 * Broadcast Device Status to Clients
 */
function broadcastDeviceStatus(eventType, data) {
    // Broadcast to all connected WebSocket clients
    // This allows the web dashboard to see device status updates in real-time
    if (wss && wss.clients) {
        const message = JSON.stringify({
            type: eventType,
            data: data,
            timestamp: new Date().toISOString()
        });
        
        wss.clients.forEach(client => {
            if (client.readyState === WebSocket.OPEN) {
                client.send(message);
            }
        });
    }
}

/**
 * Get All Registered Devices
 */
function getRegisteredDevices() {
    return Array.from(registeredDevices.values()).map(device => ({
        ...device,
        connected_at: new Date(device.connected_at),
        last_seen_ago: Math.floor((Date.now() - device.last_seen) / 1000) + 's'
    }));
}

/**
 * API Endpoint Handler
 * Add to main WebSocket message handler
 */
function handleDeviceAPI(ws, parsed) {
    const { type, device_id } = parsed;
    
    switch (type) {
        case 'device_handshake':
            handleDeviceHandshake(ws, parsed);
            break;
        case 'device_register':
            handleDeviceRegister(ws, parsed);
            break;
        case 'device_status':
            handleDeviceStatus(ws, parsed);
            break;
        case 'device_network_status':
            handleDeviceNetworkStatus(ws, parsed);
            break;
        case 'get_devices':
            ws.send(JSON.stringify({
                type: 'devices_list',
                devices: getRegisteredDevices()
            }));
            break;
        default:
            break;
    }
}

/**
 * OTA Logs Endpoint
 * GET /api/logs/ota
 * 
 * Query parameters:
 *   - device_id: Filter by device
 *   - status: Filter by status (success, failed, retrying, etc)
 *   - limit: Max results (default: 100, max: 500)
 *   - offset: Pagination offset (default: 0)
 *   - start_date: ISO date string
 *   - end_date: ISO date string
 */
function getOTALogs(req, res) {
    const filters = {
        device_id: req.query.device_id,
        status: req.query.status,
        limit: Math.min(parseInt(req.query.limit || 100), 500),
        offset: parseInt(req.query.offset || 0),
        start_date: req.query.start_date,
        end_date: req.query.end_date
    };
    
    const result = otaLogger.getOTAEvents(filters);
    res.json(result);
}

/**
 * OTA Statistics Endpoint
 * GET /api/logs/ota/stats
 */
function getOTAStatsEndpoint(req, res) {
    const stats = otaLogger.getOTAStats();
    res.json(stats);
}

/**
 * Device OTA History Endpoint
 * GET /api/device/:device_id/ota-history
 */
function getDeviceOTAHistoryEndpoint(req, res) {
    const { device_id } = req.params;
    const limit = Math.min(parseInt(req.query.limit || 50), 200);
    
    const history = otaLogger.getDeviceOTAHistory(device_id, limit);
    res.json({
        device_id,
        history,
        total: history.length
    });
}

/**
 * OTA Push Endpoint (Manual Trigger)
 * POST /api/device/:device_id/ota-push
 * 
 * Triggers OTA update on a specific device.
 * Current implementation: Logs intent, device checks on next interval
 * Future: WebSocket to push command to device directly
 * 
 * Request body:
 * {
 *   force: true,  // Force update even if same version
 *   priority: 'high'  // Optional priority level
 * }
 */
function pushOTAUpdate(req, res) {
    const { device_id } = req.params;
    const { force, priority } = req.body || {};
    
    const device = registeredDevices.get(device_id);
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    // Log the push attempt
    otaLogger.logOTAEvent('checking', device_id, device.firmware_version, device.firmware_version, 'OTA push initiated via API', {
        force: force || false,
        priority: priority || 'normal',
        triggered_by: 'api'
    });
    
    // Queue the push (for future immediate delivery)
    otaPushQueue.set(device_id, {
        version: device.firmware_version,
        timestamp: Date.now(),
        force: force || false,
        priority: priority || 'normal'
    });
    
    console.log(`[OTA Push] Queued update for ${device_id} - Next device check will trigger update`);
    
    res.json({
        status: 'push_queued',
        device_id,
        message: 'OTA update queued. Device will update on next check (within 1 hour)',
        device_info: {
            ip_address: device.ip_address,
            current_version: device.firmware_version,
            ota_status: device.ota_status
        }
    });
}

/**
 * OTA Push Immediate Endpoint
 * POST /api/device/:device_id/ota-push-immediate
 * 
 * Triggers OTA update on device immediately (if online).
 */
function pushOTAUpdateImmediate(req, res) {
    const { device_id } = req.params;
    
    const device = registeredDevices.get(device_id);
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    // Check if device is online
    const isOnline = (Date.now() - device.last_seen) < 30000; // 30 second timeout
    
    if (!isOnline) {
        otaLogger.logOTAEvent('retrying', device_id, device.firmware_version, device.firmware_version, 'OTA push immediate attempted but device is offline');
        return res.status(503).json({
            error: 'Device offline',
            device_id,
            last_seen: device.last_seen,
            status: 'scheduled_for_next_check'
        });
    }
    
    // Device is online - queue immediate push
    otaLogger.logOTAEvent('checking', device_id, device.firmware_version, device.firmware_version, 'OTA push immediate via API (device online)', {
        triggered_by: 'api_immediate'
    });
    
    console.log(`[OTA Push Immediate] Sending to ${device_id} (online at ${device.ip_address})`);
    
    res.json({
        status: 'push_sent',
        device_id,
        message: 'OTA push command sent to device immediately',
        device_info: {
            ip_address: device.ip_address,
            current_version: device.firmware_version,
            status: 'update_in_progress'
        }
    });
}

/**
 * Export OTA Logs
 * GET /api/logs/ota/export?format=json|csv
 */
function exportOTALogs(req, res) {
    const format = req.query.format || 'json';
    
    if (!['json', 'csv'].includes(format)) {
        return res.status(400).json({ error: 'Invalid format. Use json or csv' });
    }
    
    const data = otaLogger.exportLogs(format);
    
    if (format === 'csv') {
        res.setHeader('Content-Type', 'text/csv');
        res.setHeader('Content-Disposition', 'attachment; filename="ota-logs.csv"');
    } else {
        res.setHeader('Content-Type', 'application/json');
        res.setHeader('Content-Disposition', 'attachment; filename="ota-logs.json"');
    }
    
    res.send(data);
}

/**
 * OTA Settings Endpoint
 * GET /api/device/ota-settings - Get all OTA settings
 * PUT /api/device/ota-settings - Update OTA settings
 */
let otaSettings = {
    enabled: true,
    checkInterval: 3600000,        // 1 hour (ms)
    downloadTimeout: 30000,         // 30 seconds (ms)
    retryCount: 3,
    autoUpdate: false,
    allowRollback: true
};

function getOTASettings() {
    return otaSettings;
}

function setOTASettings(newSettings) {
    // Validate and merge settings
    if (typeof newSettings.enabled === 'boolean') otaSettings.enabled = newSettings.enabled;
    if (typeof newSettings.checkInterval === 'number') otaSettings.checkInterval = Math.max(60000, newSettings.checkInterval);  // Min 1 minute
    if (typeof newSettings.downloadTimeout === 'number') otaSettings.downloadTimeout = Math.max(5000, newSettings.downloadTimeout);  // Min 5 seconds
    if (typeof newSettings.retryCount === 'number') otaSettings.retryCount = Math.max(1, newSettings.retryCount);
    if (typeof newSettings.autoUpdate === 'boolean') otaSettings.autoUpdate = newSettings.autoUpdate;
    if (typeof newSettings.allowRollback === 'boolean') otaSettings.allowRollback = newSettings.allowRollback;
    
    console.log('[OTA Settings] Updated:', otaSettings);
    return otaSettings;
}

/**
 * Get Device Info Endpoint
 * GET /api/device/:device_id/info - Get full device information
 */
function getDeviceInfo(device_id) {
    const device = registeredDevices.get(device_id);
    if (!device) {
        return { error: 'Device not found' };
    }
    
    return {
        // Basic Info
        device_id: device.device_id,
        device_name: device.device_name || 'Unknown',
        device_type: device.device_type,
        connected_at: device.connected_at,
        last_seen: device.last_seen,
        last_seen_ago: Math.floor((Date.now() - device.last_seen) / 1000) + 's',
        
        // Network Info
        ip_address: device.ip_address,
        mac_address: device.mac_address,
        hostname: device.hostname,
        gateway: device.gateway,
        subnet_mask: device.subnet_mask,
        dns_servers: device.dns_servers,
        rssi: device.rssi,  // WiFi signal strength in dBm
        
        // System Info
        firmware_version: device.firmware_version,
        uptime_ms: device.uptime_ms,
        uptime_human: formatUptime(device.uptime_ms),
        
        // Memory Info
        free_heap: device.free_heap,
        total_heap: device.total_heap,
        heap_usage_percent: device.heap_usage_percent,
        
        // OTA Info
        ota_status: device.ota_status,
        ota_last_attempt: device.ota_last_attempt,
        
        // Status
        online: (Date.now() - device.last_seen) < 30000,  // Consider online if heard within 30 seconds
        network_status_updated: device.network_status_updated
    };
}

function formatUptime(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) return `${days}d ${hours % 24}h`;
    if (hours > 0) return `${hours}h ${minutes % 60}m`;
    if (minutes > 0) return `${minutes}m ${seconds % 60}s`;
    return `${seconds}s`;
}

/**
 * Get All Devices Info Endpoint
 */
function getAllDevicesInfo() {
    const devicesInfo = [];
    registeredDevices.forEach((device, device_id) => {
        devicesInfo.push(getDeviceInfo(device_id));
    });
    return devicesInfo;
}

// Export for use in server.js
module.exports = {
    handleDeviceHandshake,
    handleDeviceRegister,
    handleDeviceOTA,
    handleDeviceStatus,
    handleDeviceNetworkStatus,
    handleDeviceAPI,
    getRegisteredDevices,
    registeredDevices,
    getOTASettings,
    setOTASettings,
    getDeviceInfo,
    getAllDevicesInfo,
    getOTALogs,
    getOTAStatsEndpoint,
    getDeviceOTAHistoryEndpoint,
    pushOTAUpdate,
    pushOTAUpdateImmediate,
    exportOTALogs,
    otaPushQueue
};
