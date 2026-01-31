/**
 * 🌾 Farm Hub - Backend Server
 * 
 * Node.js Express server that:
 * - Caches weather data from Open-Meteo
 * - Proxies requests to ESP32 devices
 * - Provides unified API for the dashboard
 * - Aggregates device status
 * 
 * Run: node server.js
 */

const express = require('express');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
const http = require('http');
const WebSocket = require('ws');
const deviceAPI = require('./device-api');
const db = require('./database'); // SQLite DB Manager

const app = express();
const PORT = 3000;

// =============================================================================
// CONFIGURATION
// =============================================================================

// Default config (will be overwritten by DB)
let config = {
    farmName: "Smart Farm Hub",
    location: { 
        lat: -17.8292, 
        lon: 31.0522,
        city: 'Harare',
        region: 'Zimbabwe',
        timezone: 'Africa/Harare'
    },
    devices: {
        greenhouse: { ip: "10.0.0.163", name: "Greenhouse", type: "greenhouse" }
    },
    weather: {
        cacheMinutes: 10,
        timezone: "Africa/Harare"
    }
};

async function loadConfig() {
    try {
        const settings = await db.getAll();
        console.log('[CONFIG] Loaded settings from DB:', settings);
        
        if (settings.farm_name) config.farmName = settings.farm_name;
        if (settings.location) {
             // Parse if string, otherwise use as is
             const loc = typeof settings.location === 'string' ? JSON.parse(settings.location) : settings.location;
             config.location = { ...config.location, ...loc };
             config.weather.timezone = loc.timezone || config.weather.timezone;
        }
        // Add other mappings as needed
    } catch (err) {
        console.error('Failed to load config from DB:', err.message);
    }
}

// Initial load
loadConfig();

// =============================================================================
// WEATHER CACHE
// =============================================================================

let weatherCache = {
    data: null,
    timestamp: 0
};

async function fetchWeather(lat, lon) {
    const now = Date.now();
    const cacheMs = config.weather.cacheMinutes * 60 * 1000;
    
    // Use cached data if still valid
    if (weatherCache.data && (now - weatherCache.timestamp) < cacheMs) {
        console.log('[WEATHER] Returning cached data');
        return weatherCache.data;
    }
    
    try {
        const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min&timezone=${encodeURIComponent(config.weather.timezone)}`;
        
        console.log('[WEATHER] Fetching fresh data...');
        
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const data = await response.json();
        
        // Cache the result
        weatherCache.data = data;
        weatherCache.timestamp = now;
        
        console.log(`[WEATHER] Cached: ${data.current?.temperature_2m}°C`);
        return data;
        
    } catch (err) {
        console.error('[WEATHER] Fetch error:', err.message);
        // Return stale cache if available
        if (weatherCache.data) return weatherCache.data;
        throw err;
    }
}

// =============================================================================
// DEVICE STATUS AGGREGATION
// =============================================================================

let deviceStatus = {};

async function fetchDeviceStatus(deviceId, deviceInfo) {
    try {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 3000);
        
        const response = await fetch(`http://${deviceInfo.ip}/api/status`, {
            signal: controller.signal
        });
        clearTimeout(timeout);
        
        if (response.ok) {
            const data = await response.json();
            deviceStatus[deviceId] = {
                ...data,
                online: true,
                lastSeen: Date.now()
            };
            console.log(`[DEVICE] ${deviceId} status: ${data.temp}°C, ${data.amps}A`);
        } else {
            throw new Error(`HTTP ${response.status}`);
        }
    } catch (err) {
        // Device unavailable - no data shown until it connects
        deviceStatus[deviceId] = {
            online: false,
            error: err.message
        };
        console.warn(`[DEVICE] ${deviceId} offline: ${err.message}`);
    }
}

async function updateAllDeviceStatus() {
    const promises = Object.entries(config.devices).map(([id, info]) => 
        fetchDeviceStatus(id, info)
    );
    await Promise.allSettled(promises);
    
    // Broadcast to connected WebSocket clients
    broadcastStatus();
}

// Update device status every 30 seconds
setInterval(updateAllDeviceStatus, 30000);

// =============================================================================
// MIDDLEWARE
// =============================================================================

app.use(cors());
app.use(express.json());
app.use(express.static(path.join(__dirname, 'dashboard')));

// =============================================================================
// API ROUTES
// =============================================================================

// --- Settings API (SQLite) ---
app.get('/api/settings', async (req, res) => {
    try {
        const settings = await db.getAll();
        res.json(settings);
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

app.post('/api/settings', async (req, res) => {
    try {
        const { key, value } = req.body;
        if (!key || value === undefined) {
             return res.status(400).json({ error: 'Missing key or value' });
        }
        await db.set(key, value);
        
        // Refresh local config cache
        await loadConfig();
        
        res.json({ success: true });
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// --- Geocoding Proxy (Reverse Lookup) ---
app.get('/api/geocode', async (req, res) => {
    try {
        const { lat, lon } = req.query;
        if (!lat || !lon) return res.status(400).json({ error: 'Missing lat/lon' });
        
        const url = `https://nominatim.openstreetmap.org/reverse?format=json&lat=${lat}&lon=${lon}&zoom=14`;
        const response = await fetch(url, {
            headers: { 'User-Agent': 'SmartFarmHub/2.0' }
        });
        
        if (!response.ok) throw new Error('Geocoding service unavailable');
        const data = await response.json();
        
        // Simplify response
        const addr = data.address || {};
        
        // Priority list for "City" component
        // 1. Precise settlement names
        let mainName = addr.city || addr.town || addr.village || addr.municipality || addr.city_district || addr.hamlet || addr.suburb || addr.neighbourhood;
        
        // 2. Fallback to County if no settlement found (better than 'Unknown')
        // User prefers City, but if we are in the middle of nowhere, County is the best we have.
        if (!mainName && addr.county) {
            mainName = addr.county;
        }
        
        if (!mainName) mainName = 'Unknown Location';

        const secondary = addr.state || addr.region || addr.province || addr.state_district || '';
        
        let cityDisplay = mainName;
        if (secondary && secondary !== mainName) {
            cityDisplay += `, ${secondary}`;
        }
        
        const location = {
            cityComponent: mainName,
            stateComponent: secondary,
            country: addr.country || '',
            city: cityDisplay, // Legacy support
            display_name: data.display_name
        };
        console.log(`[GEOCODE] Resolved ${lat},${lon} to: ${cityDisplay}, ${location.country}`);
        
        res.json(location);
    } catch (err) {
        console.error('Geocode error:', err.message);
        res.status(500).json({ error: 'Failed to lookup address' });
    }
});

// --- Weather API (for ESP32s and dashboard) ---
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

// --- Device Status API ---
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

app.get('/api/devices/:deviceId', (req, res) => {
    const { deviceId } = req.params;
    
    if (!config.devices[deviceId]) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    res.json({
        ...config.devices[deviceId],
        status: deviceStatus[deviceId] || { online: false }
    });
});

// --- Config API ---
app.get('/api/config', (req, res) => {
    res.json({
        farmName: config.farmName,
        location: config.location,
        devices: config.devices
    });
});

app.post('/api/config', (req, res) => {
    // ... deprecated ... use /api/settings
    res.json({ success: true });
});

app.get('/api/dashboard', async (req, res) => {
    try {
         // Refresh weather if stale
         await fetchWeather(config.location.lat, config.location.lon);
         
         res.json({
             farmName: config.farmName,
             location: config.location,
             weather: weatherCache.data ? weatherCache.data.current : null,
             daily: weatherCache.data ? weatherCache.data.daily : null,
             devices: deviceStatus
         });
    } catch(e) {
         res.status(500).json({error: e.message});
    }
});

// --- Device Proxy ---
// Proxy requests to individual ESP32 devices
app.all('/device/:deviceId/*', async (req, res) => {
    const { deviceId } = req.params;
    const device = config.devices[deviceId];
    
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    const path = req.params[0] || '';
    const targetUrl = `http://${device.ip}/${path}`;
    
    try {
        const options = {
            method: req.method,
            headers: {
                'Content-Type': req.get('Content-Type') || 'application/json'
            }
        };
        
        if (['POST', 'PUT', 'PATCH'].includes(req.method)) {
            options.body = JSON.stringify(req.body);
        }
        
        const response = await fetch(targetUrl, options);
        const data = await response.text();
        
        res.status(response.status)
           .set('Content-Type', response.headers.get('Content-Type') || 'text/plain')
           .send(data);
           
    } catch (err) {
        res.status(502).json({ 
            error: 'Device unreachable', 
            device: deviceId,
            message: err.message 
        });
    }
});

// --- Health Check ---
app.get('/api/health', (req, res) => {
    const onlineCount = Object.values(deviceStatus).filter(d => d.online).length;
    const totalCount = Object.keys(config.devices).length;
    
    res.json({
        status: 'ok',
        uptime: process.uptime(),
        devices: { online: onlineCount, total: totalCount },
        weatherCacheAge: weatherCache.timestamp ? 
            Math.round((Date.now() - weatherCache.timestamp) / 1000) + 's' : 'none'
    });
});

// System status endpoint for settings
app.get('/api/status', (req, res) => {
    res.json({
        status: 'online',
        timestamp: Date.now(),
        uptime: Math.floor(process.uptime()),
        pi_ip: req.ip,
        version: '2.1.0'
    });
});

// =============================================================================
// OTA LOGGING ENDPOINTS
// =============================================================================

// Get OTA logs with filtering
app.get('/api/logs/ota', (req, res) => {
    deviceAPI.getOTALogs(req, res);
});

// Get OTA statistics
app.get('/api/logs/ota/stats', (req, res) => {
    deviceAPI.getOTAStatsEndpoint(req, res);
});

// Get OTA history for specific device
app.get('/api/device/:device_id/ota-history', (req, res) => {
    deviceAPI.getDeviceOTAHistoryEndpoint(req, res);
});

// Export OTA logs (JSON or CSV)
app.get('/api/logs/ota/export', (req, res) => {
    deviceAPI.exportOTALogs(req, res);
});

// Push OTA update to device (scheduled)
app.post('/api/device/:device_id/ota-push', (req, res) => {
    deviceAPI.pushOTAUpdate(req, res);
});

// Push OTA update immediately (if device online)
app.post('/api/device/:device_id/ota-push-immediate', (req, res) => {
    deviceAPI.pushOTAUpdateImmediate(req, res);
});

// =============================================================================
// DEVICE DASHBOARDS
// =============================================================================

// Unified device dashboard
app.get('/greenhouse/', (req, res) => {
    res.sendFile(path.join(__dirname, 'dashboard', 'unified.html'));
});

app.get('/coop1/', (req, res) => {
    res.sendFile(path.join(__dirname, 'dashboard', 'unified.html'));
});

app.get('/coop2/', (req, res) => {
    res.sendFile(path.join(__dirname, 'dashboard', 'unified.html'));
});

app.get('/coop3/', (req, res) => {
    res.sendFile(path.join(__dirname, 'dashboard', 'unified.html'));
});

// =============================================================================
// WEBSOCKET SERVER
// =============================================================================

const server = http.createServer(app);
const wss = new WebSocket.Server({ server, path: '/ws' });

const wsClients = new Set();

wss.on('connection', (ws, req) => {
    console.log('[WS] Client connected');
    wsClients.add(ws);
    
    // Send initial complete sync
    const now = new Date();
    ws.send(JSON.stringify({
        type: 'sync',
        farmName: config.farmName,
        devices: deviceStatus,
        config: {
            ssid: config.devices.greenhouse?.name || 'Greenhouse'
        },
        sys: {
            time: now.toISOString().split('T')[1].split('.')[0],
            valid: true
        },
        temp: deviceStatus.greenhouse?.temp || 24.5,
        amps: deviceStatus.greenhouse?.amps || 2.3,
        weather: {
            valid: !!weatherCache.data,
            temp: weatherCache.data?.current?.temperature_2m || 22,
            min: weatherCache.data?.daily?.temperature_2m_min?.[0] || 18,
            max: weatherCache.data?.daily?.temperature_2m_max?.[0] || 28,
            code: weatherCache.data?.current?.weather_code || 0,
            humi: weatherCache.data?.current?.relative_humidity_2m || 65,
            wind: weatherCache.data?.current?.wind_speed_10m || 5,
            pressure: 1013,
            cloud_cover: 50,
            visibility: 10000,
            uv_index: 5,
            feels: 21,
            is_day: 1
        }
    }));
    
    ws.on('message', (message) => {
        try {
            const data = JSON.parse(message);
            handleWsMessage(ws, data);
        } catch (err) {
            console.error('[WS] Parse error:', err);
        }
    });
    
    ws.on('close', () => {
        wsClients.delete(ws);
        console.log('[WS] Client disconnected');
    });
});

function handleWsMessage(ws, data) {
    switch (data.type) {
        case 'get_status':
            ws.send(JSON.stringify({
                type: 'status',
                devices: deviceStatus
            }));
            break;
            
        case 'device_command':
            // Forward command to a specific device
            forwardToDevice(data.deviceId, data.command);
            break;
            
        case 'refresh':
            updateAllDeviceStatus();
            break;
    }
}

async function forwardToDevice(deviceId, command) {
    const device = config.devices[deviceId];
    if (!device) return;
    
    try {
        const response = await fetch(`http://${device.ip}/api/command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(command)
        });
        console.log(`[DEVICE] Command sent to ${deviceId}:`, command);
    } catch (err) {
        console.error(`[DEVICE] Failed to send command to ${deviceId}:`, err.message);
    }
}

function broadcastStatus() {
    const now = new Date();
    const ghStatus = deviceStatus.greenhouse || {};
    
    // Convert deviceStatus object to array for frontend
    const devicesArray = Object.entries(deviceStatus).map(([id, data]) => ({
        id,
        ...data
    }));
    
    // Only include telemetry if ESP32 actually provided it
    const payload = {
        type: 'sync',
        devices: devicesArray,  // Send as array, not object
        config: {
            ssid: config.devices.greenhouse?.name || 'Greenhouse',
            city: config.location?.city || 'Unknown',
            region: config.location?.region || 'Unknown'
        },
        net: {
            connected: true,
            ssid: 'Greenhouse Network',
            ip: process.env.PI_IP || '192.168.1.100',
            rssi: -50
        },
        sys: {
            time: now.toISOString().split('T')[1].split('.')[0],  // HH:MM:SS
            valid: true
        },
        weather: {
            valid: !!weatherCache.data,
            temp: weatherCache.data?.current?.temperature_2m || null,
            min: weatherCache.data?.daily?.temperature_2m_min?.[0] || null,
            max: weatherCache.data?.daily?.temperature_2m_max?.[0] || null,
            code: weatherCache.data?.current?.weather_code || null,
            humi: weatherCache.data?.current?.relative_humidity_2m || null,
            wind: weatherCache.data?.current?.wind_speed_10m || null,
            wind_direction: weatherCache.data?.current?.wind_direction_10m || null,
            pressure: weatherCache.data?.current?.pressure_msl || null,
            cloud_cover: weatherCache.data?.current?.cloud_cover || null,
            visibility: weatherCache.data?.current?.visibility || null,
            uv_index: weatherCache.data?.daily?.uv_index_max?.[0] || null,
            feels: weatherCache.data?.current?.apparent_temperature || null,
            is_day: weatherCache.data?.current?.is_day !== undefined ? weatherCache.data.current.is_day : null,
            hourly: []
        },
        timestamp: Date.now()
    };
    
    // ONLY add temp/amps if ESP32 actually sent them (not undefined)
    if (ghStatus.temp !== undefined) {
        payload.temp = ghStatus.temp;
    }
    if (ghStatus.amps !== undefined) {
        payload.amps = ghStatus.amps;
    }
    if (ghStatus.power !== undefined) {
        payload.power = ghStatus.power;
    }
    
    const message = JSON.stringify(payload);
    
    for (const client of wsClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
        }
    }
}

// =============================================================================
// START SERVER
// =============================================================================

server.listen(PORT, '0.0.0.0', () => {
    console.log(`
╔════════════════════════════════════════════════════════════╗
║                                                            ║
║       🌾 FARM HUB SERVER                                   ║
║                                                            ║
║       Running on http://localhost:${PORT}                     ║
║                                                            ║
║       Dashboard:  http://localhost:${PORT}/                   ║
║       API:        http://localhost:${PORT}/api/               ║
║       WebSocket:  ws://localhost:${PORT}/ws                   ║
║                                                            ║
╚════════════════════════════════════════════════════════════╝
    `);
    
    console.log('Configured devices:');
    for (const [id, info] of Object.entries(config.devices)) {
        console.log(`  - ${id}: ${info.name} @ ${info.ip}`);
    }
    console.log('');
    
    // Initial device status fetch
    updateAllDeviceStatus();
    
    // Fetch weather on startup
    fetchWeather(config.location.lat, config.location.lon).catch(err => 
        console.error('[WEATHER] Initial fetch failed:', err.message)
    );
});
