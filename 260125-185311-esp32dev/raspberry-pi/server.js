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
const db = require('./database');

const app = express();
const PORT = 3000;

// =============================================================================
// CONFIGURATION
// =============================================================================

// Load device configuration from file
let config = {
    farmName: "My Farm Hub",
    location: { lat: null, lon: null }, // Will be loaded from database settings
    devices: {
        greenhouse: { ip: "192.168.1.100", name: "Greenhouse", type: "greenhouse" },
        coop1: { ip: "192.168.1.101", name: "Coop 1", type: "coop" },
        coop2: { ip: "192.168.1.102", name: "Coop 2", type: "coop" },
        coop3: { ip: "192.168.1.103", name: "Coop 3", type: "coop" }
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
// WEATHER CONVERSION & CACHING
// =============================================================================

let weatherCache = {
    data: null,
    timestamp: 0
};

// Dedicated temperature converter - called only 2 times:
// 1. When new weather data is downloaded from API
// 2. When temperature unit is changed in settings
async function convertAndCacheWeather(sourceData, isFromAPI = true) {
    try {
        // Extract temperature data from source
        let tempC, daily, hourly, timezone;
        
        if (isFromAPI) {
            // Source 1: Fresh API data
            tempC = sourceData.current?.temperature_2m || 0;
            daily = sourceData.daily || null;
            hourly = sourceData.hourly || null;
            timezone = sourceData.timezone || 'UTC';
        } else {
            // Source 2: Cached database data
            tempC = sourceData.current_temp_c || 0;
            daily = sourceData.daily ? JSON.parse(sourceData.daily) : null;
            hourly = sourceData.hourly ? JSON.parse(sourceData.hourly) : null;
            timezone = sourceData.timezone || 'UTC';
        }
        
        const tempF = Math.round((tempC * 9/5) + 32);
        const weatherCode = sourceData.current?.weather_code || (sourceData.weather_code || 0);
        
        // Convert daily temperatures
        const dailyConverted = daily ? {
            ...daily,
            temperature_2m_max_c: daily.temperature_2m_max,
            temperature_2m_max_f: daily.temperature_2m_max.map(c => Math.round((c * 9/5) + 32)),
            temperature_2m_min_c: daily.temperature_2m_min,
            temperature_2m_min_f: daily.temperature_2m_min.map(c => Math.round((c * 9/5) + 32))
        } : null;
        
        // Convert hourly temperatures
        const hourlyConverted = hourly ? {
            ...hourly,
            temperature_2m_c: hourly.temperature_2m,
            temperature_2m_f: hourly.temperature_2m.map(c => Math.round((c * 9/5) + 32))
        } : null;
        
        // Store converted data in database
        await db.saveWeatherCache(
            tempC,
            tempF,
            weatherCode,
            JSON.stringify(dailyConverted),
            JSON.stringify(hourlyConverted),
            timezone
        );
        
        console.log(`[CONVERT] Cached: ${tempC}°C / ${tempF}°F${isFromAPI ? ' (from API)' : ' (from DB)'}`);
        
        // Return converted data
        return {
            current: {
                temperature_2m_c: tempC,
                temperature_2m_f: tempF,
                weather_code: weatherCode,
                relative_humidity_2m: sourceData.current?.relative_humidity_2m,
                wind_speed_10m: sourceData.current?.wind_speed_10m
            },
            daily: dailyConverted,
            hourly: hourlyConverted,
            timezone: timezone
        };
    } catch (err) {
        console.error('[CONVERT] Error during conversion:', err);
        throw err;
    }
}

async function fetchWeather(lat, lon) {
    const now = Date.now();
    const cacheMs = config.weather.cacheMinutes * 60 * 1000;
    
    // Use cached data if still valid
    if (weatherCache.data && (now - weatherCache.timestamp) < cacheMs) {
        console.log('[WEATHER] Returning cached data');
        return weatherCache.data;
    }
    
    try {
        const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m&daily=sunrise,sunset,temperature_2m_max,temperature_2m_min&hourly=temperature_2m,weather_code&timezone=${encodeURIComponent(config.weather.timezone)}`;
        
        console.log('[WEATHER] Fetching fresh data...');
        
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const data = await response.json();
        
        // CONVERSION SOURCE 1: Fresh API data
        // Call dedicated converter when new weather data is downloaded
        const convertedData = await convertAndCacheWeather(data, true);
        
        // Cache the converted result (in-memory for fast access)
        weatherCache.data = convertedData;
        weatherCache.timestamp = now;
        
        return convertedData;
        
    } catch (err) {
        console.error('[WEATHER] Fetch error:', err.message);
        // Return stale cache if available
        if (weatherCache.data) return weatherCache.data;
        throw err;
    }
}

// =============================================================================
// NTP TIME SYNCHRONIZATION
// =============================================================================

// Sync time with NTP server and store in database
async function syncTimeWithNTP() {
    try {
        const ntpServers = ['pool.ntp.org', 'time.nist.gov', 'time.google.com'];
        
        for (const server of ntpServers) {
            try {
                // Use system ntpdate command (if available on Linux/Pi)
                const { exec } = require('child_process');
                
                exec(`ntpdate -u ${server}`, (error, stdout, stderr) => {
                    if (!error) {
                        const now = new Date();
                        const timestamp = Math.floor(now.getTime() / 1000);
                        
                        // Save to database
                        db.set('systemTime', JSON.stringify({
                            unix: timestamp,
                            iso: now.toISOString(),
                            synced: true,
                            source: server
                        }))
                        .then(() => {
                            console.log(`[NTP] Time synced with ${server}: ${now.toISOString()}`);
                        })
                        .catch(err => console.error('[NTP] Failed to save time:', err));
                    }
                });
                
                // Try next if this one fails
                continue;
            } catch (err) {
                console.warn(`[NTP] Failed with ${server}:`, err.message);
            }
        }
    } catch (err) {
        console.error('[NTP] Sync error:', err.message);
    }
}

// Sync time on startup and every 6 hours
syncTimeWithNTP();
setInterval(syncTimeWithNTP, 6 * 60 * 60 * 1000);

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
    
    // Get user's preferred temperature unit (default to C)
    let tempUnit = 'C';
    try {
        const settings = await db.getAll();
        const units = settings.units || {};
        tempUnit = units.temp || 'C';
    } catch (err) {
        console.error('[DASHBOARD] Error getting units setting:', err);
    }
    
    // Convert weather data to user's preferred unit
    let weatherForDisplay = weather;
    if (weather && tempUnit === 'F') {
        weatherForDisplay = {
            temperature_2m_c: weather.current?.temperature_2m_c,
            temperature_2m_f: weather.current?.temperature_2m_f,
            temperature_2m: weather.current?.temperature_2m_f, // Display field uses F
            weather_code: weather.current?.weather_code,
            relative_humidity_2m: weather.current?.relative_humidity_2m,
            wind_speed_10m: weather.current?.wind_speed_10m
        };
    } else if (weather) {
        weatherForDisplay = {
            temperature_2m_c: weather.current?.temperature_2m_c,
            temperature_2m_f: weather.current?.temperature_2m_f,
            temperature_2m: weather.current?.temperature_2m_c, // Display field uses C
            weather_code: weather.current?.weather_code,
            relative_humidity_2m: weather.current?.relative_humidity_2m,
            wind_speed_10m: weather.current?.wind_speed_10m
        };
    }
    
    // Build response
    const devices = {};
    for (const [id, info] of Object.entries(config.devices)) {
        devices[id] = {
            id,
            name: info.name,
            type: info.type,
            ip: info.ip,
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
        weather: weatherForDisplay,
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

// --- System Time API (for ESP32 devices to sync their time) ---
app.get('/api/time', async (req, res) => {
    try {
        const now = new Date();
        const timestamp = Math.floor(now.getTime() / 1000);
        const timezone = config.weather.timezone || 'UTC';
        
        // Get stored NTP sync info
        let syncInfo = await db.get('systemTime');
        let ntpSynced = false;
        let ntpSource = null;
        
        if (syncInfo) {
            try {
                const parsed = typeof syncInfo === 'string' ? JSON.parse(syncInfo) : syncInfo;
                ntpSynced = parsed.synced || false;
                ntpSource = parsed.source || null;
            } catch (e) {
                // JSON parse error, ignore
            }
        }
        
        res.json({
            unix: timestamp,
            iso: now.toISOString(),
            timestamp: timestamp,
            timezone: timezone,
            ntpSynced: ntpSynced,
            ntpSource: ntpSource,
            serverTime: {
                year: now.getFullYear(),
                month: now.getMonth() + 1,
                day: now.getDate(),
                hour: now.getHours(),
                minute: now.getMinutes(),
                second: now.getSeconds(),
                millisecond: now.getMilliseconds()
            }
        });
    } catch (err) {
        res.status(500).json({ error: 'Failed to get system time' });
    }
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
