#!/bin/bash
#===============================================================================
# 🌾 FARM HUB - Raspberry Pi Setup Script
# Unified IoT Hub for Organic Farm Management
# Supports: Greenhouse + 3 Chicken Coops (4x ESP32 devices)
#===============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Configuration
FARM_NAME="${FARM_NAME:-Virginia Homestead}"
DOMAIN="${DOMAIN:-farm.local}"
PI_IP=$(hostname -I | awk '{print $1}')
INSTALL_DIR="/opt/farmhub"
WEB_DIR="/var/www/farmhub"
LOG_DIR="/var/log/farmhub"

# ESP32 Device Configuration (edit these IPs after devices are set up)
GREENHOUSE_IP="${GREENHOUSE_IP:-192.168.1.100}"
COOP1_IP="${COOP1_IP:-192.168.1.101}"
COOP2_IP="${COOP2_IP:-192.168.1.102}"
COOP3_IP="${COOP3_IP:-192.168.1.103}"

echo -e "${CYAN}"
cat << "EOF"
  _____ _    ____  __  __   _   _ _   _ ____  
 |  ___/ \  |  _ \|  \/  | | | | | | | | __ ) 
 | |_ / _ \ | |_) | |\/| | | |_| | | | |  _ \ 
 |  _/ ___ \|  _ <| |  | | |  _  | |_| | |_) |
 |_|/_/   \_\_| \_\_|  |_| |_| |_|\___/|____/ 
                                              
  Organic Farm IoT Hub - Raspberry Pi Setup
EOF
echo -e "${NC}"

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  Setting up Farm Hub on Raspberry Pi${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "  Farm Name: ${YELLOW}$FARM_NAME${NC}"
echo -e "  Domain:    ${YELLOW}$DOMAIN${NC}"
echo -e "  Pi IP:     ${YELLOW}$PI_IP${NC}"
echo ""

#-------------------------------------------------------------------------------
# Step 1: System Update
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[1/8] Updating system packages...${NC}"
sudo apt update && sudo apt upgrade -y

#-------------------------------------------------------------------------------
# Step 2: Install Required Packages
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[2/8] Installing required packages...${NC}"
sudo apt install -y \
    nginx \
    certbot \
    python3-certbot-nginx \
    python3-pip \
    python3-venv \
    nodejs \
    npm \
    mosquitto \
    mosquitto-clients \
    avahi-daemon \
    sqlite3 \
    git \
    curl \
    jq

#-------------------------------------------------------------------------------
# Step 3: Create Directory Structure
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[3/8] Creating directory structure...${NC}"
sudo mkdir -p $INSTALL_DIR
sudo mkdir -p $WEB_DIR
sudo mkdir -p $LOG_DIR
sudo mkdir -p /etc/farmhub

# Set permissions
sudo chown -R $USER:$USER $INSTALL_DIR
sudo chown -R www-data:www-data $WEB_DIR
sudo chown -R $USER:$USER $LOG_DIR

#-------------------------------------------------------------------------------
# Step 4: Create Configuration File
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[4/8] Creating configuration...${NC}"

sudo tee /etc/farmhub/config.json > /dev/null << EOF
{
    "farm_name": "$FARM_NAME",
    "domain": "$DOMAIN",
    "devices": {
        "greenhouse": {
            "name": "Greenhouse",
            "ip": "$GREENHOUSE_IP",
            "port": 80,
            "type": "greenhouse",
            "icon": "🌱",
            "description": "Main greenhouse - vegetables & seedlings"
        },
        "coop1": {
            "name": "Coop Alpha",
            "ip": "$COOP1_IP",
            "port": 80,
            "type": "chicken_coop",
            "icon": "🐔",
            "description": "Layer hens - egg production"
        },
        "coop2": {
            "name": "Coop Beta",
            "ip": "$COOP2_IP",
            "port": 80,
            "type": "chicken_coop",
            "icon": "🐓",
            "description": "Broilers - meat production"
        },
        "coop3": {
            "name": "Coop Gamma",
            "ip": "$COOP3_IP",
            "port": 80,
            "type": "chicken_coop",
            "icon": "🐣",
            "description": "Nursery - chicks & young birds"
        }
    },
    "weather": {
        "cache_minutes": 10,
        "latitude": "",
        "longitude": ""
    },
    "ssl": {
        "enabled": true,
        "self_signed": true
    }
}
EOF

echo -e "${GREEN}✓ Configuration saved to /etc/farmhub/config.json${NC}"

#-------------------------------------------------------------------------------
# Step 5: Generate Self-Signed SSL Certificate
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[5/8] Generating SSL certificates...${NC}"

sudo mkdir -p /etc/nginx/ssl

# Generate self-signed certificate (valid for 10 years)
sudo openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout /etc/nginx/ssl/farmhub.key \
    -out /etc/nginx/ssl/farmhub.crt \
    -subj "/C=US/ST=Virginia/L=Farm/O=$FARM_NAME/CN=$DOMAIN" \
    2>/dev/null

echo -e "${GREEN}✓ SSL certificate generated${NC}"
echo -e "${YELLOW}  Note: For production, run: sudo certbot --nginx -d yourdomain.com${NC}"

#-------------------------------------------------------------------------------
# Step 6: Configure Nginx Reverse Proxy
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[6/8] Configuring Nginx reverse proxy...${NC}"

sudo tee /etc/nginx/sites-available/farmhub > /dev/null << 'NGINX_CONF'
# Farm Hub - Unified IoT Dashboard
# Reverse proxy for ESP32 devices with SSL termination

# Rate limiting
limit_req_zone $binary_remote_addr zone=api_limit:10m rate=10r/s;
limit_req_zone $binary_remote_addr zone=ws_limit:10m rate=5r/s;

# Upstream definitions for ESP32 devices
upstream greenhouse {
    server GREENHOUSE_IP_PLACEHOLDER:80;
    keepalive 2;
}

upstream coop1 {
    server COOP1_IP_PLACEHOLDER:80;
    keepalive 2;
}

upstream coop2 {
    server COOP2_IP_PLACEHOLDER:80;
    keepalive 2;
}

upstream coop3 {
    server COOP3_IP_PLACEHOLDER:80;
    keepalive 2;
}

# HTTP -> HTTPS redirect
server {
    listen 80;
    server_name _;
    return 301 https://$host$request_uri;
}

# Main HTTPS server
server {
    listen 443 ssl http2;
    server_name _;

    # SSL Configuration
    ssl_certificate /etc/nginx/ssl/farmhub.crt;
    ssl_certificate_key /etc/nginx/ssl/farmhub.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384;
    ssl_prefer_server_ciphers off;
    ssl_session_cache shared:SSL:10m;
    ssl_session_timeout 1d;

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;
    add_header Referrer-Policy "strict-origin-when-cross-origin" always;

    # Gzip compression
    gzip on;
    gzip_types text/plain text/css application/json application/javascript text/xml application/xml;
    gzip_min_length 1000;

    # Root for unified dashboard
    root /var/www/farmhub;
    index index.html;

    # Main dashboard
    location / {
        try_files $uri $uri/ /index.html;
    }

    # API endpoint - Weather cache (Pi handles this)
    location /api/weather {
        limit_req zone=api_limit burst=5 nodelay;
        proxy_pass http://127.0.0.1:3000/api/weather;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_cache_valid 200 10m;
    }

    # API endpoint - Device status aggregation
    location /api/devices {
        limit_req zone=api_limit burst=10 nodelay;
        proxy_pass http://127.0.0.1:3000/api/devices;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }

    # ==================== GREENHOUSE ====================
    location /greenhouse/ {
        proxy_pass http://greenhouse/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }

    location /greenhouse/ws {
        limit_req zone=ws_limit burst=3 nodelay;
        proxy_pass http://greenhouse/ws;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 86400;
    }

    # ==================== CHICKEN COOP 1 ====================
    location /coop1/ {
        proxy_pass http://coop1/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }

    location /coop1/ws {
        limit_req zone=ws_limit burst=3 nodelay;
        proxy_pass http://coop1/ws;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400;
    }

    # ==================== CHICKEN COOP 2 ====================
    location /coop2/ {
        proxy_pass http://coop2/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }

    location /coop2/ws {
        limit_req zone=ws_limit burst=3 nodelay;
        proxy_pass http://coop2/ws;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400;
    }

    # ==================== CHICKEN COOP 3 ====================
    location /coop3/ {
        proxy_pass http://coop3/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }

    location /coop3/ws {
        limit_req zone=ws_limit burst=3 nodelay;
        proxy_pass http://coop3/ws;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400;
    }

    # Error pages
    error_page 502 503 504 /error.html;
    location = /error.html {
        internal;
        root /var/www/farmhub;
    }
}
NGINX_CONF

# Replace IP placeholders
sudo sed -i "s/GREENHOUSE_IP_PLACEHOLDER/$GREENHOUSE_IP/g" /etc/nginx/sites-available/farmhub
sudo sed -i "s/COOP1_IP_PLACEHOLDER/$COOP1_IP/g" /etc/nginx/sites-available/farmhub
sudo sed -i "s/COOP2_IP_PLACEHOLDER/$COOP2_IP/g" /etc/nginx/sites-available/farmhub
sudo sed -i "s/COOP3_IP_PLACEHOLDER/$COOP3_IP/g" /etc/nginx/sites-available/farmhub

# Enable site
sudo ln -sf /etc/nginx/sites-available/farmhub /etc/nginx/sites-enabled/
sudo rm -f /etc/nginx/sites-enabled/default

# Test and reload nginx
sudo nginx -t && sudo systemctl reload nginx

echo -e "${GREEN}✓ Nginx configured${NC}"

#-------------------------------------------------------------------------------
# Step 7: Create Backend API Service (Weather Cache & Device Aggregation)
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[7/8] Setting up backend API service...${NC}"

mkdir -p $INSTALL_DIR/backend

cat > $INSTALL_DIR/backend/package.json << 'EOF'
{
    "name": "farmhub-backend",
    "version": "1.0.0",
    "description": "Farm Hub Backend - Weather Cache & Device Aggregation",
    "main": "server.js",
    "scripts": {
        "start": "node server.js",
        "dev": "node --watch server.js"
    },
    "dependencies": {
        "express": "^4.18.2",
        "node-fetch": "^2.7.0",
        "ws": "^8.14.2",
        "node-cache": "^5.1.2"
    }
}
EOF

cat > $INSTALL_DIR/backend/server.js << 'EOF'
/**
 * Farm Hub Backend Service
 * - Centralized weather data caching (reduces ESP32 load)
 * - Device status aggregation
 * - WebSocket hub for real-time updates
 */

const express = require('express');
const fetch = require('node-fetch');
const NodeCache = require('node-cache');
const WebSocket = require('ws');
const fs = require('fs');

const app = express();
const PORT = 3000;

// Load configuration
let config = {};
try {
    config = JSON.parse(fs.readFileSync('/etc/farmhub/config.json', 'utf8'));
} catch (e) {
    console.error('Failed to load config:', e.message);
    config = { devices: {}, weather: { cache_minutes: 10 } };
}

// Weather cache (10 minutes default)
const weatherCache = new NodeCache({ 
    stdTTL: (config.weather?.cache_minutes || 10) * 60 
});

// Device status cache (30 seconds)
const deviceCache = new NodeCache({ stdTTL: 30 });

app.use(express.json());

// CORS for local development
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    res.header('Access-Control-Allow-Headers', 'Origin, X-Requested-With, Content-Type, Accept');
    next();
});

/**
 * Weather API - Centralized for all ESP32 devices
 * This offloads HTTPS requests from ESP32s to the Pi
 */
app.get('/api/weather', async (req, res) => {
    const lat = req.query.lat || config.weather?.latitude || '';
    const lon = req.query.lon || config.weather?.longitude || '';
    
    if (!lat || !lon) {
        return res.status(400).json({ error: 'Missing coordinates' });
    }
    
    const cacheKey = `weather_${lat}_${lon}`;
    let weatherData = weatherCache.get(cacheKey);
    
    if (!weatherData) {
        try {
            console.log(`[Weather] Fetching fresh data for ${lat}, ${lon}`);
            const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}` +
                `&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day,apparent_temperature` +
                `&hourly=temperature_2m,weather_code,is_day` +
                `&daily=temperature_2m_max,temperature_2m_min` +
                `&forecast_days=1&temperature_unit=celsius&wind_speed_unit=kmh&timezone=auto`;
            
            const response = await fetch(url);
            weatherData = await response.json();
            weatherData._cached_at = Date.now();
            weatherCache.set(cacheKey, weatherData);
            
            console.log(`[Weather] Cached for ${config.weather?.cache_minutes || 10} minutes`);
        } catch (error) {
            console.error('[Weather] Fetch error:', error.message);
            return res.status(500).json({ error: 'Weather fetch failed' });
        }
    } else {
        console.log(`[Weather] Serving from cache`);
    }
    
    res.json(weatherData);
});

/**
 * Device Status Aggregation
 * Polls all ESP32 devices and returns unified status
 */
app.get('/api/devices', async (req, res) => {
    const devices = config.devices || {};
    const results = {};
    
    const fetchPromises = Object.entries(devices).map(async ([id, device]) => {
        const cacheKey = `device_${id}`;
        let status = deviceCache.get(cacheKey);
        
        if (!status) {
            try {
                const controller = new AbortController();
                const timeout = setTimeout(() => controller.abort(), 3000);
                
                const response = await fetch(`http://${device.ip}:${device.port}/api/status`, {
                    signal: controller.signal
                });
                clearTimeout(timeout);
                
                status = await response.json();
                status.online = true;
                deviceCache.set(cacheKey, status);
            } catch (error) {
                status = { online: false, error: error.message };
            }
        }
        
        results[id] = {
            ...device,
            status
        };
    });
    
    await Promise.all(fetchPromises);
    res.json(results);
});

/**
 * Device Status - Single device
 */
app.get('/api/devices/:id', async (req, res) => {
    const device = config.devices?.[req.params.id];
    if (!device) {
        return res.status(404).json({ error: 'Device not found' });
    }
    
    try {
        const response = await fetch(`http://${device.ip}:${device.port}/api/status`);
        const status = await response.json();
        res.json({ ...device, status, online: true });
    } catch (error) {
        res.json({ ...device, online: false, error: error.message });
    }
});

/**
 * Farm Info API
 */
app.get('/api/farm', (req, res) => {
    res.json({
        name: config.farm_name,
        domain: config.domain,
        devices: Object.keys(config.devices || {}).length,
        tagline: "Sustainable Organic Farming",
        features: [
            "🐄 Grass-fed Beef Cattle",
            "🐔 Free-range Chickens & Farm Fresh Eggs", 
            "🐐 Grass-fed Goats",
            "🌱 Organic Vegetables & Seedlings"
        ]
    });
});

// Health check
app.get('/api/health', (req, res) => {
    res.json({ 
        status: 'ok', 
        uptime: process.uptime(),
        memory: process.memoryUsage()
    });
});

// Start server
app.listen(PORT, '127.0.0.1', () => {
    console.log(`🌾 Farm Hub Backend running on port ${PORT}`);
    console.log(`   Devices configured: ${Object.keys(config.devices || {}).length}`);
});
EOF

# Install dependencies
cd $INSTALL_DIR/backend
npm install

# Create systemd service
sudo tee /etc/systemd/system/farmhub-backend.service > /dev/null << EOF
[Unit]
Description=Farm Hub Backend API
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$INSTALL_DIR/backend
ExecStart=/usr/bin/node server.js
Restart=always
RestartSec=10
Environment=NODE_ENV=production

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable farmhub-backend
sudo systemctl start farmhub-backend

echo -e "${GREEN}✓ Backend service installed${NC}"

#-------------------------------------------------------------------------------
# Step 8: Create Unified Dashboard
#-------------------------------------------------------------------------------
echo -e "\n${BLUE}[8/8] Creating unified dashboard...${NC}"

# This will be created by the separate dashboard script
cat > $INSTALL_DIR/create_dashboard.sh << 'DASHBOARD_SCRIPT'
#!/bin/bash
# Dashboard creation is handled by a separate file for maintainability
echo "Dashboard files should be copied from the development machine"
DASHBOARD_SCRIPT

chmod +x $INSTALL_DIR/create_dashboard.sh

echo -e "${GREEN}✓ Setup framework complete${NC}"

#-------------------------------------------------------------------------------
# Final Summary
#-------------------------------------------------------------------------------
echo ""
echo -e "${CYAN}╔═══════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║                    🌾 FARM HUB SETUP COMPLETE 🌾                  ║${NC}"
echo -e "${CYAN}╠═══════════════════════════════════════════════════════════════════╣${NC}"
echo -e "${CYAN}║${NC}                                                                   ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${GREEN}✓${NC} Nginx reverse proxy with SSL                                ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${GREEN}✓${NC} Backend API service (weather cache, device aggregation)     ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${GREEN}✓${NC} Configuration at /etc/farmhub/config.json                   ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}                                                                   ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}Access Points:${NC}                                                 ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Dashboard:  https://$PI_IP/                           ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Greenhouse: https://$PI_IP/greenhouse/                ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Coop Alpha: https://$PI_IP/coop1/                     ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Coop Beta:  https://$PI_IP/coop2/                     ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    • Coop Gamma: https://$PI_IP/coop3/                     ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}                                                                   ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}  ${YELLOW}Next Steps:${NC}                                                    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    1. Edit /etc/farmhub/config.json with ESP32 IPs              ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    2. Copy dashboard files to /var/www/farmhub/                 ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    3. Restart nginx: sudo systemctl restart nginx               ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}    4. For public SSL: sudo certbot --nginx -d yourdomain.com    ${CYAN}║${NC}"
echo -e "${CYAN}║${NC}                                                                   ${CYAN}║${NC}"
echo -e "${CYAN}╚═══════════════════════════════════════════════════════════════════╝${NC}"
echo ""
