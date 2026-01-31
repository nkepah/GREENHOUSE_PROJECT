/**
 * 🌾 Farm Hub - Backend Server
 * 
 * Node.js Express server that:
 * - Caches weather data from Open-Meteo
 * - Proxies requests to ESP32 devices
 * - Provides unified API for the dashboard
 * - Aggregates device status
 * 
 * Priority Management:
 * - Starts at HIGH priority (-20) for fast initial startup
 * - Drops to LOWEST priority (19) after 30 seconds for efficiency
 * 
 * Run: node server.js
 */

const express = require('express');
const os = require('os');
const cors = require('cors');
const path = require('path');
const fs = require('fs');
const http = require('http');
const WebSocket = require('ws');
const db = require('./database');

const app = express();
const PORT = 3000;

// =============================================================================
// PRIORITY MANAGEMENT
// =============================================================================

// Set high priority at startup
function setHighPriority() {
    try {
        os.setPriority(-20); // HIGH priority (only works on Linux/Unix)
        console.log('✓ Process priority set to HIGH (-20)');
    } catch (e) {
        console.log('⚠ Could not set high priority (requires Linux/Unix)');
    }
}

// Drop to lowest priority after 30 seconds
function setLowestPriority() {
    setTimeout(() => {
        try {
            os.setPriority(19); // LOWEST priority
            console.log('✓ Process priority dropped to LOWEST (19) for efficiency');
        } catch (e) {
            console.log('⚠ Could not set lowest priority');
        }
    }, 30000); // 30 seconds
}

// Initialize priority management
setHighPriority();
setLowestPriority();

// =============================================================================
// CONFIGURATION
// =============================================================================

// Load device configuration from file
let config = {
    farmName: "My Farm Hub",
    location: { lat: null, lon: null }, // Will be loaded from database settings
    devices: {
        greenhouse: { name: "Greenhouse", type: "greenhouse" },
        coop1: { name: "Coop 1", type: "coop" },
        coop2: { name: "Coop 2", type: "coop" },
        coop3: { name: "Coop 3", type: "coop" }
    },
    weather: {
        cacheMinutes: 10,
        timezone: "UTC" // Will be loaded from database settings
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
        console.log('✓ Configuration saved');
    } catch (err) {
        console.error('Failed to save config:', err.message);
    }
}

loadConfig();

// Load location and timezone from database on startup
async function initializeSettingsFromDatabase() {
    try {
        const settings = await db.getAll();
        if (settings.location) {
            const loc = typeof settings.location === 'string' ? JSON.parse(settings.location) : settings.location;
            config.location.lat = parseFloat(loc.lat);
            config.location.lon = parseFloat(loc.lon);
            config.weather.timezone = loc.timezone || 'UTC';
            console.log(`[CONFIG] Loaded from database: ${config.location.lat}, ${config.location.lon} (${config.weather.timezone})`);
        }
    } catch (err) {
        console.warn('[CONFIG] Could not load location from database:', err.message);
    }
}

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
        const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&hourly=temperature_2m,weather_code,wind_speed_10m&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min&timezone=${encodeURIComponent(config.weather.timezone)}`;
        
        console.log('[WEATHER] Fetching fresh data...');
        
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const data = await response.json();
        
        // Convert temperature_2m to both C and F for hourly data
        if (data.hourly && data.hourly.temperature_2m) {
            data.hourly.temperature_2m_c = data.hourly.temperature_2m;
            data.hourly.temperature_2m_f = data.hourly.temperature_2m.map(c => (c * 9/5) + 32);
        }
        
        // Convert daily temperatures to both C and F
        if (data.daily && data.daily.temperature_2m_max) {
            data.daily.temperature_2m_max_c = data.daily.temperature_2m_max;
            data.daily.temperature_2m_max_f = data.daily.temperature_2m_max.map(c => (c * 9/5) + 32);
        }
        if (data.daily && data.daily.temperature_2m_min) {
            data.daily.temperature_2m_min_c = data.daily.temperature_2m_min;
            data.daily.temperature_2m_min_f = data.daily.temperature_2m_min.map(c => (c * 9/5) + 32);
        }
        
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
        } else {
            throw new Error(`HTTP ${response.status}`);
        }
    } catch (err) {
        deviceStatus[deviceId] = {
            ...deviceStatus[deviceId],
            online: false,
            error: err.message
        };
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

// ========== DEVELOPMENT MODE: Disable all caching ==========
// All files served with no-cache to force fresh loads during development
app.use((req, res, next) => {
    // Disable caching for all responses during development
    res.set('Cache-Control', 'no-cache, no-store, must-revalidate, max-age=0');
    res.set('Pragma', 'no-cache');
    res.set('Expires', '-1');
    res.set('ETag', 'W/"dev-' + Date.now() + '"'); // Change ETag on every request
    next();
});

// Serve static files with maxAge=0 to disable browser/proxy caching
app.use(express.static(path.join(__dirname, 'dashboard'), {
    maxAge: 0,
    etag: false,  // Disable ETag generation for static files
    lastModified: false  // Don't set Last-Modified header
}));

// =============================================================================
// API ROUTES
// =============================================================================

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
        // Use IP from deviceStatus if available (from registration), otherwise from config
        const ip = info.ip || (deviceStatus[id]?.ip);
        devices[id] = {
            ...info,
            ip: ip || null,
            online: deviceStatus[id]?.online || false,
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
    const { farmName, location, devices } = req.body;
    
    if (farmName) config.farmName = farmName;
    if (location) config.location = location;
    if (devices) config.devices = { ...config.devices, ...devices };
    
    saveConfig();
    res.json({ success: true, config });
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

// --- Aggregated Dashboard Data ---
app.get('/api/dashboard', async (req, res) => {
    console.log('[DASHBOARD] Endpoint called');
    // Fetch fresh weather using coordinates from database
    let weather = null;
    try {
        // Make sure we have coordinates before fetching weather
        if (!config.location.lat || !config.location.lon) {
            throw new Error('Location coordinates not configured. Please set location in Settings.');
        }
        weather = await fetchWeather(config.location.lat, config.location.lon);
    } catch (err) {
        console.error('Weather fetch failed:', err);
    }
    
    // Build response - use dynamically registered IPs
    const devices = {};
    for (const [id, info] of Object.entries(config.devices)) {
        // Get IP from device registration (if available), fallback to config
        const ip = info.ip || (deviceStatus[id]?.ip);
        devices[id] = {
            id,
            name: info.name,
            type: info.type,
            ip: ip || null,
            online: deviceStatus[id]?.online || false,
            status: deviceStatus[id] || { online: false }
        };
    }
    
    // Get fresh location display by geocoding current coordinates if available
    let locationDisplay = { city: 'Unknown', address: 'Unknown' };
    console.log('[DASHBOARD] Checking location:', { lat: config.location.lat, lon: config.location.lon });
    if (config.location.lat && config.location.lon) {
        try {
            console.log('[DASHBOARD] Geocoding fresh location...');
            const geoResponse = await fetch(`https://nominatim.openstreetmap.org/reverse?format=json&lat=${config.location.lat}&lon=${config.location.lon}`, {
                headers: { 'User-Agent': 'FarmHub/2.3' }
            });
            if (geoResponse.ok) {
                const geoData = await geoResponse.json();
                const address = geoData.address || {};
                let city = address.city || address.town || address.village || address.suburb;
                if (!city && address.county) {
                    city = address.county.replace(' County', '').replace(' Parish', '').replace(' District', '');
                }
                city = city || 'Location';
                const state = address.state || '';
                const cityComponent = state ? `${city}, ${state}` : city;
                locationDisplay = { city: city, address: cityComponent };
                console.log('[DASHBOARD] Fresh geocode result:', locationDisplay);
            }
        } catch (err) {
            console.error('[DASHBOARD] Fresh geocode failed:', err.message);
            // Fallback to stored address if available
            if (config.location.address) {
                locationDisplay = { city: config.location.city || 'Location', address: config.location.address };
            }
        }
    } else {
        console.log('[DASHBOARD] No coordinates configured');
    }
    
    res.json({
        farmName: config.farmName,
        location: locationDisplay,
        weather: weather?.current || null,
        daily: weather?.daily || null,
        hourly: weather?.hourly || null,
        devices,
        timestamp: Date.now()
    });
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

// --- Reverse Geocode (Convert Lat/Lon to Address) ---
app.get('/api/geocode', async (req, res) => {
    const { lat, lon } = req.query;
    
    if (!lat || !lon) {
        return res.status(400).json({ error: 'Missing lat or lon' });
    }
    
    try {
        // Use OpenStreetMap Nominatim for reverse geocoding
        // IMPORTANT: Nominatim requires a User-Agent header
        const response = await fetch(`https://nominatim.openstreetmap.org/reverse?format=json&lat=${lat}&lon=${lon}`, {
            headers: {
                'User-Agent': 'FarmHub/2.3'
            }
        });
        
        if (!response.ok) {
            throw new Error(`Nominatim returned ${response.status}: ${response.statusText}`);
        }
        
        const data = await response.json();
        
        const address = data.address || {};
        // Try multiple fields for city: city > town > village > suburb > county (without " County" suffix)
        let city = address.city || address.town || address.village || address.suburb;
        
        // If no city found, use county but strip " County" suffix
        if (!city && address.county) {
            city = address.county.replace(' County', '').replace(' Parish', '').replace(' District', '');
        }
        
        city = city || 'Location';
        const state = address.state || '';
        const country = address.country || '';
        
        res.json({
            city,
            state,
            country,
            cityComponent: state ? `${city}, ${state}` : city,
            stateComponent: state,
            full: data.display_name
        });
    } catch (err) {
        console.error('[API] Geocode failed:', err.message);
        res.status(500).json({ 
            error: 'Geocoding failed: ' + err.message,
            city: 'Location',
            state: '',
            country: ''
        });
    }
});

// --- Settings API (Load/Save from SQLite) ---
app.get('/api/settings', async (req, res) => {
    try {
        const settings = await db.getAll();
        res.json(settings);
    } catch (err) {
        console.error('[API] Failed to load settings:', err.message);
        res.status(500).json({ error: 'Failed to load settings' });
    }
});

app.post('/api/settings', async (req, res) => {
    try {
        const { key, value } = req.body;
        if (!key) {
            return res.status(400).json({ error: 'Missing key' });
        }
        
        await db.set(key, value);
        console.log(`[API] Saved setting: ${key} = ${JSON.stringify(value).substring(0, 50)}`);
        
        // If location was updated, reload it into config
        if (key === 'location') {
            const loc = typeof value === 'string' ? JSON.parse(value) : value;
            config.location.lat = parseFloat(loc.lat);
            config.location.lon = parseFloat(loc.lon);
            config.weather.timezone = loc.timezone || 'UTC';
            console.log(`[CONFIG] Updated from settings: ${config.location.lat}, ${config.location.lon} (${config.weather.timezone})`);
        }
        
        res.json({ success: true, key, value });
    } catch (err) {
        console.error('[API] Failed to save settings:', err.message);
        res.status(500).json({ error: 'Failed to save settings' });
    }
});

// =============================================================================
// DEVICE REGISTRATION ENDPOINTS (for ESP32 devices)
// =============================================================================

// Track device registration startup times for priority management
const deviceRegistrationTimes = new Map();

// Get current process priority (simulate if on Windows/Mac)
function getCurrentPriority() {
    try {
        const priority = require('os').platform() === 'linux' ? 
            require('child_process').execSync('nice -p $$').toString().trim() : 
            'N/A';
        return priority;
    } catch (e) {
        return 'unknown';
    }
}

// Set process priority (high = -20, low = 19 on Linux; requires privilege)
function setProcessPriority(priority) {
    try {
        if (require('os').platform() === 'linux') {
            const { execSync } = require('child_process');
            // priority: 'high' = -10, 'low' = 10
            const niceValue = priority === 'high' ? -10 : 10;
            execSync(`renice ${niceValue} -p $$`);
            console.log(`[PRIORITY] Set to ${priority} (nice ${niceValue})`);
        }
    } catch (err) {
        console.log(`[PRIORITY] Could not set priority: ${err.message}`);
    }
}

// Device registration endpoint - ESP32 sends its info via HTTP POST
app.post('/api/device/register', (req, res) => {
    const { device_id, hostname, ip_address, device_type, mac_address } = req.body;
    
    if (!device_id) {
        return res.status(400).json({ error: 'Missing device_id' });
    }
    
    // First registration? Set to high priority
    if (!deviceRegistrationTimes.has(device_id)) {
        deviceRegistrationTimes.set(device_id, Date.now());
        console.log(`[DEVICE] 🔴 HIGH PRIORITY: First registration from ${device_id}`);
        setProcessPriority('high');
    }
    
    console.log(`[DEVICE] Registration from: ${device_id} (${ip_address})`);
    
    // Store device info if not already in config
    if (!config.devices[device_id]) {
        config.devices[device_id] = {
            ip: ip_address,
            name: hostname || device_id,
            type: device_type || 'device',
            mac_address: mac_address,
            registered_at: new Date().toISOString()
        };
        saveConfig();
    } else {
        // Update existing device
        config.devices[device_id].ip = ip_address;
        config.devices[device_id].last_ip_update = new Date().toISOString();
    }
    
    // Update device status
    deviceStatus[device_id] = {
        online: true,
        last_seen: Date.now(),
        ip: ip_address,
        rssi: req.body.rssi || null
    };
    
    broadcastStatus();
    res.json({ success: true, message: 'Device registered' });
});

// Device IP update endpoint - ESP32 sends periodic updates
app.post('/api/device/update-ip', (req, res) => {
    const { device_id, hostname, ip_address, mac_address, rssi } = req.body;
    
    if (!device_id) {
        return res.status(400).json({ error: 'Missing device_id' });
    }
    
    // Check if we should drop priority after 30 seconds of first registration
    if (deviceRegistrationTimes.has(device_id)) {
        const timeSinceFirstReg = Date.now() - deviceRegistrationTimes.get(device_id);
        if (timeSinceFirstReg > 30000) {
            console.log(`[DEVICE] 🟢 LOW PRIORITY: ${device_id} (${timeSinceFirstReg}ms since first registration)`);
            setProcessPriority('low');
            deviceRegistrationTimes.delete(device_id); // Only drop once
        }
    }
    
    // Update device status
    deviceStatus[device_id] = {
        online: true,
        last_seen: Date.now(),
        ip: ip_address,
        rssi: rssi || null
    };
    
    // Optionally update config if IP changed significantly
    if (config.devices[device_id]) {
        config.devices[device_id].ip = ip_address;
    }
    
    broadcastStatus();
    res.json({ success: true, message: 'IP updated' });
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
    
    // Send initial status
    ws.send(JSON.stringify({
        type: 'init',
        farmName: config.farmName,
        devices: deviceStatus
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
    const message = JSON.stringify({
        type: 'status_update',
        devices: deviceStatus,
        timestamp: Date.now()
    });
    
    for (const client of wsClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
        }
    }
}

// =============================================================================
// START SERVER
// =============================================================================

server.listen(PORT, '0.0.0.0', async () => {
    // Load location and timezone from database after server starts
    await initializeSettingsFromDatabase();
    
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
});
