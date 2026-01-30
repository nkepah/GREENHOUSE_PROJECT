/**
 * 🌾 Farm Hub v2 - Enhanced Backend Server
 * 
 * Full frontend offload + Brick-resistant OTA management
 * 
 * Features:
 * - Hosts all ESP32 frontend files (fast serving)
 * - WebSocket multiplexer (single ESP32 connection, fan-out to browsers)
 * - OTA management with rollback capability
 * - Device health monitoring
 * - Weather caching
 * 
 * Run: node server-v2.js
 */

const express = require('express');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
const http = require('http');
const WebSocket = require('ws');
const multer = require('multer');
const FormData = require('form-data');

const app = express();
const PORT = 3000;

// =============================================================================
// CONFIGURATION
// =============================================================================

let config = {
    farmName: "Smart Farm Hub",
    location: { lat: -17.8292, lon: 31.0522 },
    devices: {
        greenhouse: { 
            ip: "192.168.1.100", 
            name: "Greenhouse", 
            type: "greenhouse",
            otaConfirmTimeout: 90000,  // 90 seconds to confirm
            healthCheckInterval: 5000  // Check every 5s
        }
    },
    weather: {
        cacheMinutes: 10,
        timezone: "Africa/Harare"
    },
    ota: {
        firmwareDir: path.join(__dirname, 'firmware'),
        historyFile: path.join(__dirname, 'ota-history.json')
    }
};

const CONFIG_FILE = path.join(__dirname, 'farm-config.json');

function loadConfig() {
    try {
        if (fs.existsSync(CONFIG_FILE)) {
            const data = fs.readFileSync(CONFIG_FILE, 'utf8');
            config = { ...config, ...JSON.parse(data) };
            console.log('✓ Configuration loaded');
        }
    } catch (err) {
        console.error('Failed to load config:', err.message);
    }
}

function saveConfig() {
    try {
        fs.writeFileSync(CONFIG_FILE, JSON.stringify(config, null, 2));
    } catch (err) {
        console.error('Failed to save config:', err.message);
    }
}

loadConfig();

// Ensure firmware directory exists
if (!fs.existsSync(config.ota.firmwareDir)) {
    fs.mkdirSync(config.ota.firmwareDir, { recursive: true });
}

// =============================================================================
// OTA MANAGEMENT
// =============================================================================

let otaHistory = [];
let pendingOTA = {};  // deviceId -> { startTime, version, timeout }

function loadOTAHistory() {
    try {
        if (fs.existsSync(config.ota.historyFile)) {
            otaHistory = JSON.parse(fs.readFileSync(config.ota.historyFile, 'utf8'));
        }
    } catch (err) {
        otaHistory = [];
    }
}

function saveOTAHistory() {
    try {
        fs.writeFileSync(config.ota.historyFile, JSON.stringify(otaHistory, null, 2));
    } catch (err) {
        console.error('Failed to save OTA history:', err.message);
    }
}

loadOTAHistory();

// File upload for firmware
const storage = multer.diskStorage({
    destination: config.ota.firmwareDir,
    filename: (req, file, cb) => {
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
        cb(null, `firmware-${timestamp}.bin`);
    }
});
const upload = multer({ 
    storage,
    limits: { fileSize: 2 * 1024 * 1024 }, // 2MB max
    fileFilter: (req, file, cb) => {
        if (file.originalname.endsWith('.bin')) {
            cb(null, true);
        } else {
            cb(new Error('Only .bin files allowed'));
        }
    }
});

async function pushFirmwareToDevice(deviceId, firmwarePath, version) {
    const device = config.devices[deviceId];
    if (!device) throw new Error('Device not found');
    
    console.log(`[OTA] Starting update for ${deviceId} to version ${version}`);
    
    // Read firmware file
    const firmware = fs.readFileSync(firmwarePath);
    
    // Create form data for ElegantOTA
    const formData = new FormData();
    formData.append('firmware', firmware, {
        filename: 'firmware.bin',
        contentType: 'application/octet-stream'
    });
    
    try {
        // ElegantOTA endpoint
        const response = await fetch(`http://${device.ip}/update`, {
            method: 'POST',
            body: formData,
            headers: formData.getHeaders()
        });
        
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${await response.text()}`);
        }
        
        // Record pending OTA
        pendingOTA[deviceId] = {
            startTime: Date.now(),
            version,
            firmwarePath,
            status: 'pending',
            timeout: setTimeout(() => {
                handleOTATimeout(deviceId);
            }, device.otaConfirmTimeout || 90000)
        };
        
        // Add to history
        otaHistory.unshift({
            deviceId,
            version,
            timestamp: new Date().toISOString(),
            status: 'pending',
            firmwarePath
        });
        saveOTAHistory();
        
        console.log(`[OTA] Firmware sent to ${deviceId}, waiting for confirmation...`);
        return { success: true, message: 'Firmware uploaded, waiting for device confirmation' };
        
    } catch (err) {
        console.error(`[OTA] Failed to push firmware to ${deviceId}:`, err.message);
        throw err;
    }
}

function handleOTATimeout(deviceId) {
    const pending = pendingOTA[deviceId];
    if (!pending) return;
    
    console.log(`[OTA] ⚠️ Timeout waiting for ${deviceId} confirmation!`);
    console.log(`[OTA] Device should auto-rollback via watchdog.`);
    
    // Update history
    const historyEntry = otaHistory.find(h => 
        h.deviceId === deviceId && h.status === 'pending'
    );
    if (historyEntry) {
        historyEntry.status = 'timeout-rollback';
        historyEntry.error = 'Device did not confirm within timeout. Watchdog should trigger rollback.';
        saveOTAHistory();
    }
    
    delete pendingOTA[deviceId];
    
    // Broadcast status to UI
    broadcastOTAStatus(deviceId, 'timeout-rollback');
}

function confirmOTA(deviceId, version, uptime) {
    const pending = pendingOTA[deviceId];
    
    if (pending) {
        clearTimeout(pending.timeout);
        console.log(`[OTA] ✓ ${deviceId} confirmed update to ${version} (uptime: ${uptime}s)`);
        
        // Update history
        const historyEntry = otaHistory.find(h => 
            h.deviceId === deviceId && h.status === 'pending'
        );
        if (historyEntry) {
            historyEntry.status = 'confirmed';
            historyEntry.confirmedAt = new Date().toISOString();
            historyEntry.uptimeAtConfirm = uptime;
            saveOTAHistory();
        }
        
        delete pendingOTA[deviceId];
        broadcastOTAStatus(deviceId, 'confirmed');
    }
    
    // Update device status
    if (deviceStatus[deviceId]) {
        deviceStatus[deviceId].firmwareVersion = version;
        deviceStatus[deviceId].lastConfirmed = Date.now();
    }
}

function broadcastOTAStatus(deviceId, status) {
    const message = JSON.stringify({
        type: 'ota_status',
        deviceId,
        status,
        timestamp: Date.now()
    });
    
    for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
        }
    }
}

// =============================================================================
// DEVICE STATUS & WEBSOCKET MULTIPLEXER
// =============================================================================

let deviceStatus = {};
let deviceWebSockets = {};  // deviceId -> WebSocket connection to ESP32
let browserClients = new Set();  // Browser WebSocket connections

function connectToDevice(deviceId) {
    const device = config.devices[deviceId];
    if (!device) return;
    
    // Close existing connection if any
    if (deviceWebSockets[deviceId]) {
        try {
            deviceWebSockets[deviceId].close();
        } catch (e) {}
    }
    
    console.log(`[WS] Connecting to ${deviceId} at ${device.ip}...`);
    
    try {
        const ws = new WebSocket(`ws://${device.ip}/ws`);
        
        ws.on('open', () => {
            console.log(`[WS] ✓ Connected to ${deviceId}`);
            deviceStatus[deviceId] = { 
                ...deviceStatus[deviceId], 
                online: true, 
                lastSeen: Date.now() 
            };
            broadcastDeviceStatus();
        });
        
        ws.on('message', (data) => {
            try {
                const message = JSON.parse(data.toString());
                handleDeviceMessage(deviceId, message);
            } catch (err) {
                console.error(`[WS] Parse error from ${deviceId}:`, err.message);
            }
        });
        
        ws.on('close', () => {
            console.log(`[WS] Disconnected from ${deviceId}`);
            deviceStatus[deviceId] = { 
                ...deviceStatus[deviceId], 
                online: false 
            };
            broadcastDeviceStatus();
            
            // Reconnect after 5 seconds
            setTimeout(() => connectToDevice(deviceId), 5000);
        });
        
        ws.on('error', (err) => {
            console.error(`[WS] Error with ${deviceId}:`, err.message);
        });
        
        deviceWebSockets[deviceId] = ws;
        
    } catch (err) {
        console.error(`[WS] Failed to connect to ${deviceId}:`, err.message);
        setTimeout(() => connectToDevice(deviceId), 5000);
    }
}

function handleDeviceMessage(deviceId, message) {
    // Store latest state
    if (message.type === 'sync' || message.relays !== undefined) {
        deviceStatus[deviceId] = {
            ...deviceStatus[deviceId],
            ...message,
            online: true,
            lastSeen: Date.now()
        };
        
        // Check for OTA confirmation beacon
        if (message.otaConfirm && message.version) {
            confirmOTA(deviceId, message.version, message.uptime || 0);
        }
    }
    
    // Fan-out to all browser clients
    const outMessage = JSON.stringify({
        type: 'device_update',
        deviceId,
        data: message,
        timestamp: Date.now()
    });
    
    for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(outMessage);
        }
    }
}

function broadcastDeviceStatus() {
    const message = JSON.stringify({
        type: 'status_update',
        devices: deviceStatus,
        timestamp: Date.now()
    });
    
    for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
        }
    }
}

function forwardToBrowser(deviceId, message) {
    const outMessage = JSON.stringify({
        type: 'device_data',
        deviceId,
        data: message,
        timestamp: Date.now()
    });
    
    for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(outMessage);
        }
    }
}

function forwardToDevice(deviceId, message) {
    const ws = deviceWebSockets[deviceId];
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(message));
        return true;
    }
    return false;
}

// Connect to all devices on startup
function connectAllDevices() {
    for (const deviceId of Object.keys(config.devices)) {
        connectToDevice(deviceId);
    }
}

// =============================================================================
// WEATHER CACHE (unchanged from original)
// =============================================================================

let weatherCache = { data: null, timestamp: 0 };

async function fetchWeather(lat, lon) {
    const now = Date.now();
    const cacheMs = config.weather.cacheMinutes * 60 * 1000;
    
    if (weatherCache.data && (now - weatherCache.timestamp) < cacheMs) {
        return weatherCache.data;
    }
    
    try {
        const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min&hourly=temperature_2m,precipitation_probability&timezone=${encodeURIComponent(config.weather.timezone)}`;
        
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const data = await response.json();
        weatherCache.data = data;
        weatherCache.timestamp = now;
        
        return data;
    } catch (err) {
        if (weatherCache.data) return weatherCache.data;
        throw err;
    }
}

// =============================================================================
// MIDDLEWARE
// =============================================================================

app.use(cors());
app.use(express.json());

// Serve ESP32 frontend files (greenhouse dashboard)
app.use('/greenhouse', express.static(path.join(__dirname, 'greenhouse-ui')));

// Serve unified dashboard
app.use(express.static(path.join(__dirname, 'dashboard')));

// =============================================================================
// API ROUTES - OTA Management
// =============================================================================

// Upload firmware
app.post('/api/ota/upload', upload.single('firmware'), async (req, res) => {
    try {
        if (!req.file) {
            return res.status(400).json({ error: 'No firmware file provided' });
        }
        
        const deviceId = req.body.deviceId || 'greenhouse';
        const version = req.body.version || `v${Date.now()}`;
        
        console.log(`[OTA] Received firmware upload for ${deviceId}: ${req.file.filename}`);
        
        res.json({ 
            success: true, 
            filename: req.file.filename,
            path: req.file.path,
            deviceId,
            version,
            message: 'Firmware uploaded. Use /api/ota/push to deploy.'
        });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Push firmware to device
app.post('/api/ota/push', async (req, res) => {
    try {
        const { deviceId, firmwarePath, version } = req.body;
        
        if (!deviceId || !firmwarePath) {
            return res.status(400).json({ error: 'deviceId and firmwarePath required' });
        }
        
        const result = await pushFirmwareToDevice(deviceId, firmwarePath, version || 'unknown');
        res.json(result);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Combined upload and push
app.post('/api/ota/update', upload.single('firmware'), async (req, res) => {
    try {
        if (!req.file) {
            return res.status(400).json({ error: 'No firmware file provided' });
        }
        
        const deviceId = req.body.deviceId || 'greenhouse';
        const version = req.body.version || `build-${Date.now()}`;
        
        console.log(`[OTA] Starting update: ${deviceId} -> ${version}`);
        
        const result = await pushFirmwareToDevice(deviceId, req.file.path, version);
        res.json(result);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// Get OTA history
app.get('/api/ota/history', (req, res) => {
    const deviceId = req.query.deviceId;
    let history = otaHistory;
    
    if (deviceId) {
        history = history.filter(h => h.deviceId === deviceId);
    }
    
    res.json(history.slice(0, 50)); // Last 50 entries
});

// Get pending OTA status
app.get('/api/ota/status', (req, res) => {
    const status = {};
    for (const [deviceId, pending] of Object.entries(pendingOTA)) {
        status[deviceId] = {
            version: pending.version,
            startTime: pending.startTime,
            elapsed: Date.now() - pending.startTime,
            status: pending.status
        };
    }
    res.json(status);
});

// Manual rollback request (tells device to rollback)
app.post('/api/ota/rollback', async (req, res) => {
    const { deviceId } = req.body;
    const device = config.devices[deviceId];
    
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    try {
        // Send rollback command to ESP32
        const response = await fetch(`http://${device.ip}/api/rollback`, {
            method: 'POST'
        });
        
        if (response.ok) {
            res.json({ success: true, message: 'Rollback initiated' });
        } else {
            throw new Error(`Device returned ${response.status}`);
        }
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// List available firmware files
app.get('/api/ota/firmware', (req, res) => {
    try {
        const files = fs.readdirSync(config.ota.firmwareDir)
            .filter(f => f.endsWith('.bin'))
            .map(f => {
                const stats = fs.statSync(path.join(config.ota.firmwareDir, f));
                return {
                    filename: f,
                    size: stats.size,
                    modified: stats.mtime
                };
            })
            .sort((a, b) => b.modified - a.modified);
        
        res.json(files);
    } catch (err) {
        res.json([]);
    }
});

// =============================================================================
// API ROUTES - Device Proxy
// =============================================================================

// Forward any request to device
app.all('/device/:deviceId/*', async (req, res) => {
    const { deviceId } = req.params;
    const device = config.devices[deviceId];
    
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    const devicePath = req.params[0] || '';
    const targetUrl = `http://${device.ip}/${devicePath}`;
    
    try {
        const options = {
            method: req.method,
            headers: { 'Content-Type': 'application/json' }
        };
        
        if (['POST', 'PUT', 'PATCH'].includes(req.method) && req.body) {
            options.body = JSON.stringify(req.body);
        }
        
        const response = await fetch(targetUrl, options);
        const data = await response.text();
        
        res.status(response.status)
           .set('Content-Type', response.headers.get('Content-Type') || 'text/plain')
           .send(data);
    } catch (err) {
        res.status(502).json({ error: 'Device unreachable', message: err.message });
    }
});

// =============================================================================
// API ROUTES - Weather & Status
// =============================================================================

app.get('/api/weather', async (req, res) => {
    try {
        const lat = req.query.lat || config.location.lat;
        const lon = req.query.lon || config.location.lon;
        const data = await fetchWeather(lat, lon);
        res.json(data);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.get('/api/devices', (req, res) => {
    const devices = {};
    for (const [id, info] of Object.entries(config.devices)) {
        devices[id] = {
            ...info,
            status: deviceStatus[id] || { online: false }
        };
    }
    res.json(devices);
});

app.get('/api/health', (req, res) => {
    const onlineCount = Object.values(deviceStatus).filter(d => d.online).length;
    const totalCount = Object.keys(config.devices).length;
    
    res.json({
        status: 'ok',
        uptime: process.uptime(),
        devices: { online: onlineCount, total: totalCount },
        pendingOTA: Object.keys(pendingOTA),
        weatherCacheAge: weatherCache.timestamp ? 
            Math.round((Date.now() - weatherCache.timestamp) / 1000) + 's' : 'none'
    });
});

// =============================================================================
// WEBSOCKET SERVER - Browser connections
// =============================================================================

const server = http.createServer(app);
const wss = new WebSocket.Server({ server, path: '/ws' });

wss.on('connection', (ws, req) => {
    console.log('[WS] Browser client connected');
    browserClients.add(ws);
    
    // Send initial status
    ws.send(JSON.stringify({
        type: 'init',
        farmName: config.farmName,
        devices: deviceStatus,
        pendingOTA: Object.keys(pendingOTA)
    }));
    
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            handleBrowserMessage(ws, data);
        } catch (err) {
            console.error('[WS] Browser message parse error:', err);
        }
    });
    
    ws.on('close', () => {
        browserClients.delete(ws);
        console.log('[WS] Browser client disconnected');
    });
});

function handleBrowserMessage(ws, data) {
    switch (data.type) {
        case 'device_command':
            // Forward command to specific device
            const success = forwardToDevice(data.deviceId, data.command);
            ws.send(JSON.stringify({ 
                type: 'command_ack', 
                deviceId: data.deviceId, 
                success 
            }));
            break;
            
        case 'get_status':
            ws.send(JSON.stringify({
                type: 'status_update',
                devices: deviceStatus
            }));
            break;
            
        case 'refresh':
            // Re-request status from all devices
            for (const deviceId of Object.keys(deviceWebSockets)) {
                forwardToDevice(deviceId, { action: 'getStatus' });
            }
            break;
    }
}

// =============================================================================
// START SERVER
// =============================================================================

server.listen(PORT, '0.0.0.0', () => {
    console.log(`
╔════════════════════════════════════════════════════════════╗
║                                                            ║
║       🌾 FARM HUB v2 - Enhanced Server                     ║
║                                                            ║
║       Dashboard:    http://localhost:${PORT}/                 ║
║       Greenhouse:   http://localhost:${PORT}/greenhouse/      ║
║       OTA Manager:  http://localhost:${PORT}/ota.html         ║
║       WebSocket:    ws://localhost:${PORT}/ws                 ║
║       API:          http://localhost:${PORT}/api/             ║
║                                                            ║
╚════════════════════════════════════════════════════════════╝
    `);
    
    console.log('Configured devices:');
    for (const [id, info] of Object.entries(config.devices)) {
        console.log(`  - ${id}: ${info.name} @ ${info.ip}`);
    }
    
    // Connect to all ESP32 devices
    connectAllDevices();
});
