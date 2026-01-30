# 🌾 Farm Hub - Raspberry Pi IoT Gateway

A complete reverse proxy and unified dashboard for managing multiple ESP32-based farm automation systems with HTTPS encryption.

## 🚜 Features

- **HTTPS Encryption** - Self-signed or Let's Encrypt SSL certificates
- **Unified Dashboard** - Single view of all ESP32 devices
- **Weather Caching** - Offloads HTTPS requests from ESP32s to the Pi
- **Device Aggregation** - Real-time status from all connected devices
- **WebSocket Proxying** - Real-time updates through the proxy

## 🐄 About Your Farm

This system is designed for **organic farming operations** featuring:

- 🐄 **Grass-fed Beef Cattle** - Sustainably raised on open pastures
- 🐔 **Free-range Chickens** - Happy hens producing farm-fresh eggs  
- 🐐 **Grass-fed Goats** - Naturally raised for milk and meat
- 🌱 **Organic Vegetables** - Greenhouse-grown seasonal produce

---

## 🌾 Suggested Farm Names

Choose a name that reflects your organic, sustainable farming values:

| # | Name | Description |
|---|------|-------------|
| 1 | **Verdant Valley Farms** | Lush, green, thriving |
| 2 | **Greenfield Homestead** | Classic, traditional |
| 3 | **Sunrise Organic Ranch** | Fresh beginnings each day |
| 4 | **Heritage Pastures** | Time-honored traditions |
| 5 | **Meadow Creek Farm** | Peaceful, pastoral |
| 6 | **Rolling Hills Ranch** | Scenic, spacious |
| 7 | **Oakwood Organics** | Strong, rooted, natural |
| 8 | **Golden Harvest Homestead** | Abundant, prosperous |
| 9 | **Freedom Range Farm** | Free-range philosophy |
| 10 | **Wildflower Meadows** | Natural, diverse, beautiful |

---

## 📦 Quick Start

### Prerequisites

- Raspberry Pi 4 (2GB+ RAM recommended)
- Raspberry Pi OS (Bullseye or newer)
- Network connection
- ESP32 devices on the same network

### Installation

1. **Copy files to Raspberry Pi:**
   ```bash
   scp -r raspberry-pi/* pi@YOUR_PI_IP:~/farmhub/
   ```

2. **Run the setup script:**
   ```bash
   ssh pi@YOUR_PI_IP
   cd ~/farmhub
   sudo python3 setup_complete.py
   ```

3. **Follow the interactive prompts** to configure:
   - Farm name
   - ESP32 device IPs
   - Weather location coordinates

### Alternative: Bash Script

```bash
# Edit the IP addresses first
nano setup_farm_hub.sh

# Make executable and run
chmod +x setup_farm_hub.sh
sudo ./setup_farm_hub.sh
```

---

## 🌐 Network Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        INTERNET                                  │
│                           │                                      │
│                    ┌──────▼──────┐                              │
│                    │   Router    │                              │
│                    └──────┬──────┘                              │
│                           │                                      │
│              ┌────────────┼────────────┐                        │
│              │    Local Network        │                        │
│              │                         │                        │
│     ┌────────▼────────┐               │                        │
│     │  Raspberry Pi   │               │                        │
│     │   (Gateway)     │               │                        │
│     │                 │               │                        │
│     │ ┌─────────────┐ │               │                        │
│     │ │   Nginx     │ │               │                        │
│     │ │  (HTTPS)    │ │               │                        │
│     │ └──────┬──────┘ │               │                        │
│     │        │        │               │                        │
│     │ ┌──────▼──────┐ │               │                        │
│     │ │  Backend    │ │               │                        │
│     │ │ (Weather,   │ │               │                        │
│     │ │  Caching)   │ │               │                        │
│     │ └─────────────┘ │               │                        │
│     └────────┬────────┘               │                        │
│              │                         │                        │
│    ┌─────────┼─────────┬──────────────┤                        │
│    │         │         │              │                        │
│ ┌──▼──┐  ┌───▼───┐ ┌───▼───┐  ┌──────▼──────┐                 │
│ │ESP32│  │ESP32  │ │ESP32  │  │   ESP32     │                 │
│ │Green│  │Coop 1 │ │Coop 2 │  │   Coop 3    │                 │
│ │house│  │       │ │       │  │             │                 │
│ └─────┘  └───────┘ └───────┘  └─────────────┘                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 🔒 SSL/HTTPS Options

### Option 1: Self-Signed Certificate (Default)

Generated automatically during setup. Works immediately but shows browser warning.

### Option 2: Let's Encrypt (For Public Access)

If you have a domain name pointing to your Pi:

```bash
sudo certbot --nginx -d yourdomain.com -d www.yourdomain.com
```

### Option 3: Local CA

For trusted local certificates without warnings, set up a local Certificate Authority.

---

## 📊 Access Points

After installation, access your farm systems at:

| System | URL | Description |
|--------|-----|-------------|
| Dashboard | `https://PI_IP/` | Unified view of all devices |
| Greenhouse | `https://PI_IP/greenhouse/` | Full greenhouse controls |
| Coop Alpha | `https://PI_IP/coop1/` | Layer hen coop |
| Coop Beta | `https://PI_IP/coop2/` | Broiler coop |
| Coop Gamma | `https://PI_IP/coop3/` | Nursery coop |
| API Health | `https://PI_IP/api/health` | Backend status |
| Weather API | `https://PI_IP/api/weather?lat=X&lon=Y` | Cached weather |

---

## ⚙️ Configuration

Edit `/etc/farmhub/config.json`:

```json
{
    "farm_name": "Verdant Valley Farms",
    "domain": "farm.local",
    "devices": {
        "greenhouse": {
            "name": "Greenhouse",
            "ip": "192.168.1.100",
            "port": 80,
            "type": "greenhouse",
            "icon": "🌱",
            "description": "Main greenhouse"
        },
        "coop1": {
            "name": "Coop Alpha",
            "ip": "192.168.1.101",
            "port": 80,
            "type": "chicken_coop",
            "icon": "🐔",
            "description": "Layer hens"
        }
    },
    "weather": {
        "cache_minutes": 10,
        "latitude": "37.7749",
        "longitude": "-122.4194"
    }
}
```

After editing, restart nginx:
```bash
sudo systemctl restart nginx
```

---

## 🐔 Chicken Coop ESP32 Setup

Each chicken coop ESP32 controls:

| Feature | GPIO | Description |
|---------|------|-------------|
| **Lights** | 2, 4, 5 | LED strips or bulbs for lighting schedule |
| **Door Actuator** | 12, 13 | Linear actuator for automatic door |
| **Feed Dispenser** | 14, 27 | Servo/motor for feed release |
| **Temperature Sensor** | 32 | DS18B20 for monitoring |
| **Water Level** | 34 | Analog sensor for water monitoring |

The same firmware base as the greenhouse can be adapted for coops.

---

## 🌡️ Weather Load Sharing

The Raspberry Pi handles weather API requests to:

1. **Reduce ESP32 HTTPS overhead** - ESP32 doesn't need SSL for weather
2. **Cache responses** - One request serves all 4 ESP32s
3. **Provide fallback** - Pi continues working if one ESP32 fails

### ESP32 Weather via Pi

Instead of calling Open-Meteo directly, ESP32s call:
```
http://PI_IP:3000/api/weather?lat=XX&lon=YY
```

---

## 🔧 Service Management

```bash
# Check status
sudo systemctl status nginx
sudo systemctl status farmhub-backend

# View logs
sudo journalctl -u farmhub-backend -f
sudo tail -f /var/log/nginx/error.log

# Restart services
sudo systemctl restart nginx
sudo systemctl restart farmhub-backend

# Update ESP32 IPs
sudo nano /etc/farmhub/config.json
sudo systemctl restart nginx
```

---

## 🛠️ Troubleshooting

### Device shows offline
1. Check ESP32 is powered and connected to WiFi
2. Verify IP address in config: `ping 192.168.1.XXX`
3. Check nginx logs: `sudo tail -f /var/log/nginx/error.log`

### SSL certificate warning
- Self-signed certificates always show warnings
- For trusted cert: `sudo certbot --nginx -d yourdomain.com`

### WebSocket not connecting
1. Check nginx WebSocket config has `proxy_set_header Upgrade`
2. Verify ESP32 WebSocket is on `/ws` endpoint
3. Check browser console for errors

### Weather not loading
1. Check coordinates in config
2. Test API directly: `curl http://localhost:3000/api/weather?lat=37&lon=-122`
3. Check backend logs: `sudo journalctl -u farmhub-backend -f`

---

## 📁 File Structure

```
/opt/farmhub/
├── backend/
│   ├── server.js       # Node.js API server
│   └── package.json
│
/var/www/farmhub/
├── index.html          # Unified dashboard
└── error.html          # Offline device page

/etc/farmhub/
└── config.json         # Device configuration

/etc/nginx/
├── sites-available/
│   └── farmhub         # Nginx config
└── ssl/
    ├── farmhub.crt     # SSL certificate
    └── farmhub.key     # SSL private key
```

---

## 📜 License

MIT License - Feel free to adapt for your farm!

---

🌾 **Happy Farming!** 🐄🐔🐐🌱
