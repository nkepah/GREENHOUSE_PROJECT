/**
 * 🌿 Greenhouse Proxy Server
 * 
 * Raspberry Pi server that:
 * 1. Serves frontend HTML/CSS/JS (offloaded from ESP32)
 * 2. Proxies WebSocket connections (single ESP32 connection, fan-out to browsers)
 * 3. Caches weather data (reduces ESP32 HTTPS burden)
 * 4. Provides REST API proxy to ESP32
 * 
 * This dramatically improves UI responsiveness by:
 * - Serving files from Pi's fast filesystem instead of ESP32's slow LittleFS
 * - Maintaining single persistent WebSocket to ESP32
 * - Fanning out data to multiple browser clients without overloading ESP32
 * 
 * @author Farm Automation
 * @version 2.0.0
 */

const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const path = require('path');
const fs = require('fs');
const compression = require('compression');

// =============================================================================
// CONFIGURATION
// =============================================================================

const CONFIG_FILE = path.join(__dirname, 'config.json');

let config = {
    // Server settings
    port: 3000,
    
    // ESP32 Greenhouse connection
    esp32: {
        ip: '10.0.0.163',
        wsPort: 80,
        httpPort: 80,
        reconnectInterval: 5000,  // ms between reconnection attempts
        pingInterval: 30000,      // ms between ping frames
        name: 'Greenhouse'
    },
    
    // Weather caching
    weather: {
        enabled: true,
        cacheMinutes: 10,
        location: { lat: '38.130254613092', lon: '-77.73829202505', city: 'Detecting...', region: 'Detecting...' },
        timezone: 'Africa/Harare',
        unit: 'celsius'
    },
    
    // Logging
    logging: {
        level: 'info',  // debug, info, warn, error
        timestamps: true
    }
};

// Load configuration from file
function loadConfig() {
    try {
        if (fs.existsSync(CONFIG_FILE)) {
            const data = JSON.parse(fs.readFileSync(CONFIG_FILE, 'utf8'));
            config = { ...config, ...data };
            log('info', 'Configuration loaded from', CONFIG_FILE);
        } else {
            // Create default config file
            saveConfig();
            log('info', 'Created default configuration file');
        }
    } catch (err) {
        log('error', 'Failed to load config:', err.message);
    }
}

function saveConfig() {
    try {
        fs.writeFileSync(CONFIG_FILE, JSON.stringify(config, null, 2));
    } catch (err) {
        log('error', 'Failed to save config:', err.message);
    }
}

// =============================================================================
// LOGGING
// =============================================================================

const LOG_LEVELS = { debug: 0, info: 1, warn: 2, error: 3 };

function log(level, ...args) {
    if (LOG_LEVELS[level] >= LOG_LEVELS[config.logging.level]) {
        const timestamp = config.logging.timestamps 
            ? `[${new Date().toISOString()}]` 
            : '';
        const prefix = `${timestamp}[${level.toUpperCase()}]`;
        console.log(prefix, ...args);
    }
}

// =============================================================================
// EXPRESS APP SETUP
// =============================================================================

const app = express();
const server = http.createServer(app);

// Enable gzip compression for all responses
app.use(compression());

// Parse JSON bodies
app.use(express.json());

// CORS headers for development
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    res.header('Access-Control-Allow-Headers', 'Content-Type');
    if (req.method === 'OPTIONS') return res.sendStatus(200);
    next();
});

// Request logging
app.use((req, res, next) => {
    log('debug', `${req.method} ${req.url}`);
    next();
});

// =============================================================================
// STATIC FILE SERVING (Frontend Offload)
// =============================================================================

// Serve frontend files with aggressive caching
const FRONTEND_DIR = path.join(__dirname, 'public');

// Ensure public directory exists
if (!fs.existsSync(FRONTEND_DIR)) {
    fs.mkdirSync(FRONTEND_DIR, { recursive: true });
    log('warn', 'Created empty public directory. Copy frontend files here.');
}

// Serve static files with cache headers
app.use(express.static(FRONTEND_DIR, {
    maxAge: '1h',
    etag: true,
    lastModified: true,
    setHeaders: (res, filePath) => {
        // No-cache for HTML files to ensure fresh content
        if (filePath.endsWith('.html')) {
            res.setHeader('Cache-Control', 'no-cache, no-store, must-revalidate');
        }
    }
}));

// Explicit routes for main pages
app.get('/', (req, res) => {
    res.sendFile(path.join(FRONTEND_DIR, 'index.html'));
});

app.get('/routines', (req, res) => {
    res.sendFile(path.join(FRONTEND_DIR, 'routines.html'));
});

app.get('/alerts', (req, res) => {
    res.sendFile(path.join(FRONTEND_DIR, 'alerts.html'));
});

// API Documentation page
app.get('/api', (req, res) => {
    res.sendFile(path.join(__dirname, 'api-docs.html'));
});

// =============================================================================
// REVERSE GEOCODING (Get city/region from coordinates)
// =============================================================================

let locationCache = {
    city: 'Unknown',
    region: 'Unknown'
};

async function reverseGeocode(lat, lon) {
    try {
        // Try Nominatim first with user-agent header
        const url = `https://nominatim.openstreetmap.org/reverse?format=json&lat=${lat}&lon=${lon}`;
        const response = await fetch(url, {
            headers: { 'User-Agent': 'GreenhouseOS/1.0' }
        });
        if (response.ok) {
            const data = await response.json();
            if (data && data.address) {
                const addr = data.address;
                let city = addr.city || addr.town || addr.village || addr.county || 'Unknown';
                let region = addr.state || addr.province || 'Unknown';
                
                locationCache = { city, region };
                log('info', `[Location] Reverse geocoded: ${city}, ${region}`);
                config.weather.location.city = city;
                config.weather.location.region = region;
                saveConfig();
                
                return { city, region };
            }
        }
    } catch (err) {
        log('warn', '[Location] Nominatim failed:', err.message);
    }
    
    // Fallback: Try IP-based geolocation (ipapi.co - free, no rate limit)
    try {
        const response = await fetch('https://ipapi.co/json/');
        if (response.ok) {
            const data = await response.json();
            if (data.city && data.region) {
                locationCache = { city: data.city, region: data.region };
                log('info', `[Location] IP geolocation: ${data.city}, ${data.region}`);
                config.weather.location.city = data.city;
                config.weather.location.region = data.region;
                config.weather.location.lat = data.latitude;
                config.weather.location.lon = data.longitude;
                saveConfig();
                
                return locationCache;
            }
        }
    } catch (err) {
        log('warn', '[Location] IP geolocation failed:', err.message);
    }
    
    return locationCache;
}

// =============================================================================
// WEATHER CACHING
// =============================================================================

let weatherCache = {
    data: null,
    timestamp: 0,
    fetching: false
};

async function fetchWeather() {
    if (weatherCache.fetching) return weatherCache.data;
    
    const now = Date.now();
    const cacheMs = config.weather.cacheMinutes * 60 * 1000;
    
    // Return cached data if still fresh
    if (weatherCache.data && (now - weatherCache.timestamp) < cacheMs) {
        return weatherCache.data;
    }
    
    weatherCache.fetching = true;
    
    try {
        const { lat, lon } = config.weather.location;
        const tempUnit = config.weather.unit === 'fahrenheit' ? 'fahrenheit' : 'celsius';
        
        // 2-day forecast with hourly and daily data - include extended weather fields
        const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}` +
            `&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m,pressure_msl,cloud_cover,visibility,uv_index,is_day,apparent_temperature` +
            `&hourly=temperature_2m,weather_code,is_day,precipitation_probability,relative_humidity_2m,wind_speed_10m,wind_direction_10m,pressure_msl,cloud_cover,visibility,uv_index` +
            `&daily=temperature_2m_max,temperature_2m_min,weather_code,precipitation_sum,sunrise,sunset,uv_index_max,precipitation_probability_max,wind_speed_10m_max,wind_direction_10m_dominant` +
            `&forecast_days=2&temperature_unit=${tempUnit}&wind_speed_unit=kmh&timezone=auto`;
        
        log('info', '[Weather] Fetching 2-day forecast...');
        
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        
        const apiData = await response.json();
        
        // Transform to match ESP32 format expected by frontend
        const current = apiData.current || {};
        const daily = apiData.daily || {};
        const hourly = apiData.hourly || {};
        
        const weatherData = {
            type: 'weather',
            data: {
                valid: true,
                city: config.weather.location.city || 'Unknown',
                region: config.weather.location.region || 'Unknown',
                temp: current.temperature_2m,
                humi: current.relative_humidity_2m,
                code: current.weather_code,
                wind: current.wind_speed_10m,
                wind_direction: current.wind_direction_10m,
                pressure: current.pressure_msl,
                cloud_cover: current.cloud_cover,
                visibility: current.visibility,
                uv_index: current.uv_index,
                is_day: current.is_day || 1,
                feels: current.apparent_temperature,
                max: daily.temperature_2m_max?.[0] || 0,
                min: daily.temperature_2m_min?.[0] || 0,
                hourly: [],
                daily: []
            }
        };
        
        // Add 2-day daily forecast
        if (daily.time) {
            for (let i = 0; i < daily.time.length; i++) {
                weatherData.data.daily.push({
                    date: daily.time[i],
                    max: daily.temperature_2m_max?.[i],
                    min: daily.temperature_2m_min?.[i],
                    code: daily.weather_code?.[i],
                    precip: daily.precipitation_sum?.[i] || 0,
                    precip_prob: daily.precipitation_probability_max?.[i] || 0,
                    uv_index: daily.uv_index_max?.[i] || 0,
                    wind_speed: daily.wind_speed_10m_max?.[i] || 0,
                    wind_direction: daily.wind_direction_10m_dominant?.[i] || 0,
                    sunrise: daily.sunrise?.[i],
                    sunset: daily.sunset?.[i]
                });
            }
        }
        
        // Add hourly forecast (next 48 hours for 2-day coverage)
        if (hourly.time) {
            const currentHour = new Date().getHours();
            for (let i = currentHour; i < Math.min(currentHour + 48, hourly.time.length); i++) {
                const timeStr = hourly.time[i] || '';
                const hourIdx = timeStr.indexOf('T');
                weatherData.data.hourly.push({
                    time: hourIdx > 0 ? timeStr.substring(hourIdx + 1, hourIdx + 6) : '',
                    date: hourIdx > 0 ? timeStr.substring(0, hourIdx) : '',
                    temp: hourly.temperature_2m?.[i],
                    code: hourly.weather_code?.[i],
                    is_day: hourly.is_day?.[i],
                    precip_prob: hourly.precipitation_probability?.[i] || 0,
                    humi: hourly.relative_humidity_2m?.[i] || 0,
                    wind: hourly.wind_speed_10m?.[i] || 0,
                    wind_dir: hourly.wind_direction_10m?.[i] || 0,
                    pressure: hourly.pressure_msl?.[i] || 0,
                    cloud_cover: hourly.cloud_cover?.[i] || 0,
                    visibility: hourly.visibility?.[i] || 0,
                    uv_index: hourly.uv_index?.[i] || 0
                });
            }
        }
        
        weatherCache.data = weatherData;
        weatherCache.timestamp = now;
        
        log('info', `[Weather] Cached: ${current.temperature_2m}°C`);
        
        return weatherData;
        
    } catch (err) {
        log('error', '[Weather] Fetch failed:', err.message);
        return weatherCache.data || { type: 'weather', data: { valid: false } };
    } finally {
        weatherCache.fetching = false;
    }
}

// Weather API endpoint (ESP32 can fetch from here instead of direct HTTPS)
// Query parameters:
//   ?type=current  - Only current conditions
//   ?type=hourly   - Only hourly forecast (12 hours)
//   ?type=daily    - Only daily forecast (2 days)
//   (no param)     - Full data (current + hourly + daily)
app.get('/api/weather', async (req, res) => {
    try {
        const fullData = await fetchWeather();
        const type = req.query.type || 'full';
        
        if (type === 'current') {
            // Return only current conditions for lightweight requests
            const current = {
                type: 'weather',
                data: {
                    valid: fullData.data?.valid || false,
                    temp: fullData.data?.temp,
                    humi: fullData.data?.humi,
                    code: fullData.data?.code,
                    wind: fullData.data?.wind,
                    wind_direction: fullData.data?.wind_direction,
                    pressure: fullData.data?.pressure,
                    cloud_cover: fullData.data?.cloud_cover,
                    visibility: fullData.data?.visibility,
                    uv_index: fullData.data?.uv_index,
                    is_day: fullData.data?.is_day,
                    feels: fullData.data?.feels,
                    max: fullData.data?.max,
                    min: fullData.data?.min
                }
            };
            res.json(current);
        } else if (type === 'hourly') {
            // Return only hourly forecast
            const hourly = {
                type: 'weather',
                data: {
                    valid: fullData.data?.valid || false,
                    hourly: fullData.data?.hourly || []
                }
            };
            res.json(hourly);
        } else if (type === 'daily') {
            // Return only daily forecast
            const daily = {
                type: 'weather',
                data: {
                    valid: fullData.data?.valid || false,
                    daily: fullData.data?.daily || []
                }
            };
            res.json(daily);
        } else {
            // Return full data
            res.json(fullData);
        }
    } catch (err) {
        res.status(500).json({ error: err.message });
    }
});

// =============================================================================
// TIME SYNC - Pi handles NTP, broadcasts to ESP32 and browsers
// =============================================================================

let timeConfig = {
    timezone: config.weather?.timezone || 'Africa/Harare',
    gmtOffset: 2 * 3600,  // GMT+2 in seconds
    dstOffset: 0
};

// Send time sync to ESP32 periodically
function syncTimeToESP32() {
    if (!esp32Connected) return;
    
    const now = new Date();
    const timeData = {
        type: 'time_sync',
        unix: Math.floor(now.getTime() / 1000),
        iso: now.toISOString(),
        local: now.toLocaleString('en-US', { timeZone: timeConfig.timezone }),
        gmt: timeConfig.gmtOffset,
        dst: timeConfig.dstOffset
    };
    
    sendToESP32(JSON.stringify(timeData));
    log('debug', `[Time] Synced to ESP32: ${timeData.local}`);
}

// Time endpoint for browsers
app.get('/api/time', (req, res) => {
    const now = new Date();
    res.json({
        unix: Math.floor(now.getTime() / 1000),
        iso: now.toISOString(),
        local: now.toLocaleString('en-US', { timeZone: timeConfig.timezone }),
        timezone: timeConfig.timezone
    });
});

// =============================================================================
// ESP32 WEBSOCKET CONNECTION (Single persistent connection)
// =============================================================================

let esp32Socket = null;
let esp32Connected = false;
let esp32LastMessage = 0;
let esp32ReconnectTimer = null;
let esp32PingTimer = null;
let esp32TimeSyncTimer = null;
let lastSyncData = null;  // Cache last sync for new browser clients

function connectToESP32() {
    if (esp32Socket && esp32Socket.readyState === WebSocket.OPEN) {
        return; // Already connected
    }
    
    const wsUrl = `ws://${config.esp32.ip}:${config.esp32.wsPort}/ws`;
    log('info', `[ESP32] Connecting to ${wsUrl}...`);
    
    try {
        esp32Socket = new WebSocket(wsUrl);
        
        esp32Socket.on('open', () => {
            log('info', '[ESP32] ✓ Connected');
            esp32Connected = true;
            esp32LastMessage = Date.now();
            
            // Clear reconnect timer
            if (esp32ReconnectTimer) {
                clearTimeout(esp32ReconnectTimer);
                esp32ReconnectTimer = null;
            }
            
            // Start ping timer
            esp32PingTimer = setInterval(() => {
                if (esp32Socket && esp32Socket.readyState === WebSocket.OPEN) {
                    esp32Socket.ping();
                }
            }, config.esp32.pingInterval);
            
            // Start time sync timer (every 60 seconds)
            syncTimeToESP32();  // Sync immediately
            esp32TimeSyncTimer = setInterval(syncTimeToESP32, 60000);
            
            // Notify browser clients
            broadcastToBrowsers(JSON.stringify({
                type: 'proxy_status',
                esp32Connected: true,
                esp32Ip: config.esp32.ip
            }));
        });
        
        esp32Socket.on('message', (data) => {
            esp32LastMessage = Date.now();
            const message = data.toString();
            
            // Parse and handle different message types
            try {
                const parsed = JSON.parse(message);
                if (parsed.type === 'sync') {
                    lastSyncData = parsed;
                    // Add location info from server config to sync message
                    if (config.weather && config.weather.location) {
                        if (!lastSyncData.config) lastSyncData.config = {};
                        lastSyncData.config.city = config.weather.location.city;
                        lastSyncData.config.region = config.weather.location.region;
                    }
                }
                // Cache routines when ESP32 sends them
                if (parsed.type === 'routines_sync' && parsed.routines) {
                    updateRoutinesCache(parsed.routines);
                }
                // FILTER: Don't forward ESP32 weather - Pi handles weather now
                if (parsed.type === 'weather') {
                    log('debug', '[ESP32] Weather message filtered (Pi handles weather)');
                    return;  // Don't forward to browsers
                }
            } catch (e) {}
            
            // Fan out to all browser clients (use enhanced lastSyncData if applicable)
            const msgToSend = (message.includes('"type":"sync"') && lastSyncData) 
                ? JSON.stringify(lastSyncData)
                : message;
            broadcastToBrowsers(msgToSend);
            
            log('debug', `[ESP32] → Browsers: ${msgToSend.substring(0, 100)}...`);
        });
        
        esp32Socket.on('close', (code, reason) => {
            log('warn', `[ESP32] Disconnected: ${code} ${reason}`);
            handleESP32Disconnect();
        });
        
        esp32Socket.on('error', (err) => {
            log('error', '[ESP32] WebSocket error:', err.message);
            handleESP32Disconnect();
        });
        
        esp32Socket.on('pong', () => {
            log('debug', '[ESP32] Pong received');
        });
        
    } catch (err) {
        log('error', '[ESP32] Connection failed:', err.message);
        handleESP32Disconnect();
    }
}

function handleESP32Disconnect() {
    esp32Connected = false;
    
    // Clear ping timer
    if (esp32PingTimer) {
        clearInterval(esp32PingTimer);
        esp32PingTimer = null;
    }
    
    // Clear time sync timer
    if (esp32TimeSyncTimer) {
        clearInterval(esp32TimeSyncTimer);
        esp32TimeSyncTimer = null;
    }
    
    // Notify browser clients
    broadcastToBrowsers(JSON.stringify({
        type: 'proxy_status',
        esp32Connected: false,
        reconnecting: true
    }));
    
    // Schedule reconnection
    if (!esp32ReconnectTimer) {
        esp32ReconnectTimer = setTimeout(() => {
            esp32ReconnectTimer = null;
            connectToESP32();
        }, config.esp32.reconnectInterval);
        
        log('info', `[ESP32] Reconnecting in ${config.esp32.reconnectInterval}ms...`);
    }
}

function sendToESP32(message) {
    if (esp32Socket && esp32Socket.readyState === WebSocket.OPEN) {
        esp32Socket.send(message);
        log('debug', `[ESP32] ← Browser: ${message.substring(0, 100)}...`);
        return true;
    }
    log('warn', '[ESP32] Cannot send - not connected');
    return false;
}

// =============================================================================
// BROWSER WEBSOCKET SERVER (Fan-out to multiple clients)
// =============================================================================

const browserWss = new WebSocket.Server({ server, path: '/ws' });
const browserClients = new Set();

browserWss.on('connection', (ws, req) => {
    const clientIp = req.socket.remoteAddress;
    log('info', `[Browser] Client connected from ${clientIp}`);
    browserClients.add(ws);
    
    // Send proxy status
    ws.send(JSON.stringify({
        type: 'proxy_status',
        esp32Connected,
        esp32Ip: config.esp32.ip,
        proxyVersion: '2.0.0'
    }));
    
    // Send cached sync data if available
    if (lastSyncData) {
        ws.send(JSON.stringify(lastSyncData));
    }
    
    // Send cached weather if available
    if (weatherCache.data) {
        ws.send(JSON.stringify(weatherCache.data));
    }
    
    ws.on('message', (data) => {
        const message = data.toString();
        log('debug', `[Browser] Message: ${message.substring(0, 100)}...`);
        
        // Handle proxy-specific commands
        try {
            const parsed = JSON.parse(message);
            
            // Weather refresh can be handled by Pi
            if (parsed.type === 'refresh_weather') {
                fetchWeather().then(weatherData => {
                    ws.send(JSON.stringify(weatherData));
                });
                return;
            }
            
            // Handle location updates from browser
            if (parsed.type === 'set_location') {
                const lat = parsed.lat;
                const lon = parsed.lon;
                
                if (lat && lon) {
                    config.weather.location.lat = lat;
                    config.weather.location.lon = lon;
                    saveConfig();
                    
                    log('info', `[Location] Updated to ${lat}, ${lon}`);
                    
                    // Reverse geocode to get city/region
                    reverseGeocode(lat, lon).then(location => {
                        if (location) {
                            config.weather.location.city = location.city;
                            config.weather.location.region = location.region;
                            saveConfig();
                            
                            // Fetch fresh weather with new location
                            return fetchWeather();
                        }
                    }).then(weatherData => {
                        if (weatherData) {
                            broadcastToBrowsers(JSON.stringify(weatherData));
                        }
                    }).catch(err => {
                        log('error', '[Location] Reverse geocode failed:', err.message);
                    });
                }
                return;
            }
            
            // Get proxy status
            if (parsed.type === 'get_proxy_status') {
                ws.send(JSON.stringify({
                    type: 'proxy_status',
                    esp32Connected,
                    esp32Ip: config.esp32.ip,
                    browserClients: browserClients.size,
                    uptime: process.uptime(),
                    weatherCacheAge: weatherCache.timestamp 
                        ? Math.round((Date.now() - weatherCache.timestamp) / 1000) 
                        : null
                }));
                return;
            }
        } catch (e) {}
        
        // Forward all other messages to ESP32
        sendToESP32(message);
    });
    
    ws.on('close', () => {
        browserClients.delete(ws);
        log('info', `[Browser] Client disconnected. Active: ${browserClients.size}`);
    });
    
    ws.on('error', (err) => {
        log('error', '[Browser] WebSocket error:', err.message);
        browserClients.delete(ws);
    });
});

function broadcastToBrowsers(message) {
    let sent = 0;
    for (const client of browserClients) {
        if (client.readyState === WebSocket.OPEN) {
            client.send(message);
            sent++;
        }
    }
    if (sent > 0) {
        log('debug', `[Broadcast] Sent to ${sent} browser clients`);
    }
}

// =============================================================================
// REST API PROXY (Forward HTTP requests to ESP32)
// =============================================================================

// Proxy any /esp32/* requests to the actual ESP32
app.all('/esp32/*', async (req, res) => {
    const path = req.params[0] || '';
    const targetUrl = `http://${config.esp32.ip}:${config.esp32.httpPort}/${path}`;
    
    try {
        const options = {
            method: req.method,
            headers: { 'Content-Type': 'application/json' }
        };
        
        if (['POST', 'PUT', 'PATCH'].includes(req.method) && req.body) {
            options.body = JSON.stringify(req.body);
        }
        
        log('debug', `[Proxy] ${req.method} ${targetUrl}`);
        
        const response = await fetch(targetUrl, options);
        const data = await response.text();
        
        res.status(response.status)
           .set('Content-Type', response.headers.get('Content-Type') || 'application/json')
           .send(data);
           
    } catch (err) {
        log('error', '[Proxy] Request failed:', err.message);
        res.status(502).json({ 
            error: 'ESP32 unreachable', 
            message: err.message 
        });
    }
});

// =============================================================================
// STATUS & HEALTH ENDPOINTS
// =============================================================================

app.get('/api/status', (req, res) => {
    res.json({
        status: 'ok',
        version: '2.0.0',
        uptime: process.uptime(),
        esp32: {
            connected: esp32Connected,
            ip: config.esp32.ip,
            lastMessage: esp32LastMessage 
                ? Math.round((Date.now() - esp32LastMessage) / 1000) + 's ago'
                : 'never'
        },
        browsers: {
            connected: browserClients.size
        },
        weather: {
            cached: !!weatherCache.data,
            age: weatherCache.timestamp 
                ? Math.round((Date.now() - weatherCache.timestamp) / 1000) + 's'
                : null
        }
    });
});

app.get('/api/config', (req, res) => {
    // Return config without sensitive data
    res.json({
        esp32: { ip: config.esp32.ip, name: config.esp32.name },
        weather: config.weather,
        logging: { level: config.logging.level }
    });
});

app.post('/api/config', (req, res) => {
    try {
        const updates = req.body;
        
        // Update ESP32 IP if provided
        if (updates.esp32?.ip) {
            config.esp32.ip = updates.esp32.ip;
            // Reconnect to new IP
            if (esp32Socket) {
                esp32Socket.close();
            }
            setTimeout(connectToESP32, 1000);
        }
        
        // Update weather location if provided
        if (updates.weather?.location) {
            config.weather.location = updates.weather.location;
            weatherCache.timestamp = 0; // Force refresh
        }
        
        saveConfig();
        res.json({ success: true, config: config });
    } catch (err) {
        res.status(400).json({ error: err.message });
    }
});

// =============================================================================
// PI OTA UPDATE ENDPOINTS
// =============================================================================

const { exec } = require('child_process');
const multer = require('multer');

// Create uploads directory in the app folder (not /tmp which may be read-only)
const UPLOADS_DIR = path.join(__dirname, 'uploads');
if (!fs.existsSync(UPLOADS_DIR)) {
    fs.mkdirSync(UPLOADS_DIR, { recursive: true });
}
const upload = multer({ dest: UPLOADS_DIR });

// Get current version and update status
app.get('/api/system', (req, res) => {
    const pkg = require('./package.json');
    exec('git log -1 --format="%H %s" 2>/dev/null || echo "not a git repo"', { cwd: __dirname }, (err, stdout) => {
        res.json({
            version: pkg.version,
            name: pkg.name,
            uptime: process.uptime(),
            memory: process.memoryUsage(),
            gitCommit: stdout.trim(),
            nodeVersion: process.version,
            platform: process.platform
        });
    });
});

// Pull latest from git (if using git deployment)
app.post('/api/update/git', async (req, res) => {
    log('info', '[OTA] Git pull requested');
    
    exec('git pull origin main 2>&1', { cwd: __dirname }, (err, stdout, stderr) => {
        if (err) {
            log('error', '[OTA] Git pull failed:', stderr || err.message);
            return res.status(500).json({ success: false, error: stderr || err.message });
        }
        
        log('info', '[OTA] Git pull result:', stdout);
        
        // Check if package.json changed (need npm install)
        if (stdout.includes('package.json')) {
            exec('npm install --production 2>&1', { cwd: __dirname }, (err2, stdout2) => {
                if (err2) {
                    return res.status(500).json({ success: false, error: 'npm install failed' });
                }
                res.json({ success: true, output: stdout, npmInstall: stdout2, restart: true });
            });
        } else {
            res.json({ success: true, output: stdout, restart: stdout.includes('Updating') });
        }
    });
});

// Restart the service
app.post('/api/update/restart', (req, res) => {
    log('info', '[OTA] Restart requested');
    res.json({ success: true, message: 'Restarting in 2 seconds...' });
    
    setTimeout(() => {
        exec('sudo systemctl restart greenhouse-proxy', (err) => {
            if (err) log('error', '[OTA] Restart failed:', err.message);
        });
    }, 2000);
});

// Upload new frontend files
app.post('/api/update/frontend', upload.array('files'), (req, res) => {
    if (!req.files || req.files.length === 0) {
        return res.status(400).json({ error: 'No files uploaded' });
    }
    
    const results = [];
    for (const file of req.files) {
        const destPath = path.join(FRONTEND_DIR, file.originalname);
        try {
            fs.renameSync(file.path, destPath);
            results.push({ file: file.originalname, success: true });
            log('info', `[OTA] Updated frontend: ${file.originalname}`);
        } catch (err) {
            results.push({ file: file.originalname, success: false, error: err.message });
        }
    }
    
    res.json({ success: true, files: results });
});

// Upload and apply config
app.post('/api/update/config', (req, res) => {
    try {
        const newConfig = req.body;
        if (!newConfig) {
            return res.status(400).json({ error: 'No config provided' });
        }
        
        // Merge with existing config
        Object.assign(config, newConfig);
        saveConfig();
        
        res.json({ success: true, message: 'Config updated. Restart to apply all changes.' });
    } catch (err) {
        res.status(400).json({ error: err.message });
    }
});

// =============================================================================
// ROUTINE MANAGEMENT (Pi as backup/coordinator, ESP32 still executes)
// =============================================================================

const ROUTINES_FILE = path.join(__dirname, 'routines.json');
let cachedRoutines = [];

function loadRoutines() {
    try {
        if (fs.existsSync(ROUTINES_FILE)) {
            cachedRoutines = JSON.parse(fs.readFileSync(ROUTINES_FILE, 'utf8'));
            log('info', `[Routines] Loaded ${cachedRoutines.length} routines from backup`);
        }
    } catch (err) {
        log('error', '[Routines] Failed to load backup:', err.message);
    }
}

function saveRoutines() {
    try {
        fs.writeFileSync(ROUTINES_FILE, JSON.stringify(cachedRoutines, null, 2));
        log('debug', `[Routines] Saved ${cachedRoutines.length} routines to backup`);
    } catch (err) {
        log('error', '[Routines] Failed to save backup:', err.message);
    }
}

// Get all routines (from cache or ESP32)
app.get('/api/routines', async (req, res) => {
    // Return cached routines (synced from ESP32)
    res.json({ routines: cachedRoutines });
});

// Sync routines from ESP32 (called when ESP32 sends routines_sync)
function updateRoutinesCache(routines) {
    cachedRoutines = routines || [];
    saveRoutines();
}

// Forward routine commands to ESP32
app.post('/api/routines/:action', (req, res) => {
    const action = req.params.action;
    const data = req.body;
    
    let wsMessage;
    switch (action) {
        case 'create':
            wsMessage = { type: 'create_routine', ...data };
            break;
        case 'update':
            wsMessage = { type: 'update_routine', ...data };
            break;
        case 'delete':
            wsMessage = { type: 'delete_routine', id: data.id };
            break;
        case 'run':
            wsMessage = { type: 'run_routine', id: data.id };
            break;
        case 'stop':
            wsMessage = { type: 'stop_routine', id: data.id };
            break;
        default:
            return res.status(400).json({ error: 'Unknown action' });
    }
    
    if (sendToESP32(JSON.stringify(wsMessage))) {
        res.json({ success: true, message: `Routine ${action} sent to ESP32` });
    } else {
        res.status(503).json({ error: 'ESP32 not connected' });
    }
});

// Direct relay control API (for testing/manual control)
app.post('/api/relay', (req, res) => {
    const { channel, state } = req.body;
    if (!channel || state === undefined) {
        return res.status(400).json({ error: 'channel and state required' });
    }
    
    const wsMessage = { type: 'relay', channel: parseInt(channel), state: Boolean(state) };
    if (sendToESP32(JSON.stringify(wsMessage))) {
        res.json({ success: true, channel, state });
    } else {
        res.status(503).json({ error: 'ESP32 not connected' });
    }
});

// =============================================================================
// START SERVER
// =============================================================================

loadConfig();
loadRoutines();  // Load cached routines

server.listen(config.port, '0.0.0.0', () => {
    console.log(`
╔════════════════════════════════════════════════════════════════════╗
║                                                                    ║
║   🌿 GREENHOUSE PROXY SERVER v2.0                                  ║
║                                                                    ║
║   Dashboard:     http://localhost:${config.port}/                        ║
║   Routines:      http://localhost:${config.port}/routines                ║
║   Alerts:        http://localhost:${config.port}/alerts                  ║
║   WebSocket:     ws://localhost:${config.port}/ws                        ║
║   Status API:    http://localhost:${config.port}/api/status              ║
║                                                                    ║
║   ESP32 Target:  ${config.esp32.ip}                                  ║
║                                                                    ║
╚════════════════════════════════════════════════════════════════════╝
    `);
    
    // Reverse geocode location to get city/region from coordinates
    if (config.weather.location.lat && config.weather.location.lon) {
        reverseGeocode(config.weather.location.lat, config.weather.location.lon)
            .catch(err => log('warn', '[Location] Failed to reverse geocode:', err.message));
    }
    
    // Connect to ESP32
    connectToESP32();
    
    // Pre-fetch weather
    if (config.weather.enabled) {
        fetchWeather();
        
        // Broadcast weather to all browsers every 5 minutes
        // (fetches fresh data if cache expired, otherwise sends cached)
        setInterval(async () => {
            if (browserClients.size > 0) {
                const weather = await fetchWeather();
                broadcastToBrowsers(JSON.stringify(weather));
                log('debug', `[Weather] Broadcast to ${browserClients.size} browsers`);
            }
        }, 5 * 60 * 1000);  // 5 minutes
    }
});

// Graceful shutdown
process.on('SIGINT', () => {
    log('info', 'Shutting down...');
    if (esp32Socket) esp32Socket.close();
    browserWss.close();
    server.close();
    process.exit(0);
});
