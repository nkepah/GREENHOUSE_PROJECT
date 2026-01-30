# 🌿 Greenhouse Proxy Server

**Raspberry Pi WebSocket proxy and frontend server for ESP32 Greenhouse Automation**

This server dramatically improves UI responsiveness by offloading heavy tasks from the ESP32:
- Serves frontend HTML/CSS/JS files (100x faster than ESP32's LittleFS)
- Maintains single persistent WebSocket connection to ESP32
- Fans out real-time data to multiple browser clients
- Caches weather data to reduce ESP32 HTTPS burden

---

## 📊 Architecture Overview

```
                    ┌─────────────────────────────────────────────┐
                    │              RASPBERRY PI                    │
                    │                                              │
   Browser 1 ──────►│  ┌────────────────────────────────────┐     │
                    │  │     GREENHOUSE PROXY SERVER        │     │
   Browser 2 ──────►│  │                                    │     │
                    │  │  ┌──────────┐    ┌─────────────┐   │     │
   Browser 3 ──────►│  │  │ Express  │    │  WebSocket  │   │     │
                    │  │  │ (HTTP)   │    │  Server     │   │     │
                    │  │  └────┬─────┘    └──────┬──────┘   │     │
                    │  │       │                 │          │     │
                    │  │  ┌────▼─────────────────▼──────┐   │     │
                    │  │  │     WebSocket Client        │   │     │
                    │  │  │   (Single ESP32 Connection) │   │     │
                    │  │  └─────────────┬───────────────┘   │     │
                    │  └────────────────│───────────────────┘     │
                    └───────────────────│─────────────────────────┘
                                        │
                                        │ Single WebSocket
                                        │
                                        ▼
                    ┌─────────────────────────────────────────────┐
                    │                  ESP32                       │
                    │                                              │
                    │  - Relay control (15 channels)               │
                    │  - Current sensing (ACS712)                  │
                    │  - Temperature sensors (DS18B20)             │
                    │  - Routine engine                            │
                    │  - Alert triggers                            │
                    │                                              │
                    └─────────────────────────────────────────────┘
```

---

## 🚀 Quick Start

### Prerequisites

- Raspberry Pi 3/4/5 (2GB+ RAM recommended)
- Raspberry Pi OS (Bullseye or newer)
- ESP32 greenhouse controller on the same network
- Node.js 18+ (installed automatically)

### One-Line Install

```bash
# SSH into your Pi
ssh nkepah@100.92.151.67

# Download and run installer
curl -sSL https://raw.githubusercontent.com/your-repo/greenhouse-proxy/main/install.sh | sudo bash -s -- --esp32-ip=10.0.0.163
```

### Manual Installation

```bash
# 1. Copy files to Pi
scp -r greenhouse-proxy nkepah@100.92.151.67:~/

# 2. SSH into Pi
ssh nkepah@100.92.151.67

# 3. Run installer
cd ~/greenhouse-proxy
chmod +x install.sh
sudo ./install.sh
```

### Deploy from Windows/Mac

```bash
# From your development machine (defaults already configured)
./deploy.sh

# Or explicitly:
./deploy.sh nkepah@100.92.151.67
```

---

## 📁 File Structure

```
greenhouse-proxy/
├── server.js              # Main proxy server
├── package.json           # npm dependencies
├── config.json            # Configuration file
├── greenhouse-proxy.service # Systemd service definition
├── install.sh             # Full installation script
├── deploy.sh              # Remote deployment script
├── copy-frontend.sh       # Copy ESP32 frontend files
├── update-frontend.sh     # Quick frontend update
├── public/                # Frontend files (copied from ESP32)
│   ├── index.html         # Main dashboard
│   ├── routines.html      # Automation routines
│   └── alerts.html        # Alert configuration
└── README.md              # This file
```

---

## ⚙️ Configuration

Edit `/opt/greenhouse-proxy/config.json`:

```json
{
    "port": 3000,
    
    "esp32": {
        "ip": "10.0.0.163",
        "wsPort": 80,
        "httpPort": 80,
        "reconnectInterval": 5000,
        "pingInterval": 30000,
        "name": "Greenhouse"
    },
    
    "weather": {
        "enabled": true,
        "cacheMinutes": 10,
        "location": {
            "lat": "-17.8292",
            "lon": "31.0522"
        },
        "timezone": "Africa/Harare",
        "unit": "celsius"
    },
    
    "logging": {
        "level": "info",
        "timestamps": true
    }
}
```

### Configuration Options

| Option | Description | Default |
|--------|-------------|---------|
| `port` | HTTP server port | `3000` |
| `esp32.ip` | ESP32 IP address | `10.0.0.163` |
| `esp32.reconnectInterval` | ms between reconnection attempts | `5000` |
| `esp32.pingInterval` | ms between WebSocket pings | `30000` |
| `weather.cacheMinutes` | Weather cache duration | `10` |
| `weather.location.lat` | Latitude for weather | Your location |
| `weather.location.lon` | Longitude for weather | Your location |
| `logging.level` | Log verbosity: debug/info/warn/error | `info` |

After editing, restart the service:

```bash
sudo systemctl restart greenhouse-proxy
```

---

## 🖥️ Usage

### Access the Dashboard

| URL | Description |
|-----|-------------|
| `http://pi-ip:3000/` | Main dashboard |
| `http://pi-ip:3000/routines` | Automation routines |
| `http://pi-ip:3000/alerts` | Alert configuration |
| `http://pi-ip:3000/api/status` | Proxy status JSON |

### Service Management

```bash
# View logs (live)
sudo journalctl -u greenhouse-proxy -f

# Restart service
sudo systemctl restart greenhouse-proxy

# Stop service
sudo systemctl stop greenhouse-proxy

# Start service
sudo systemctl start greenhouse-proxy

# Check status
sudo systemctl status greenhouse-proxy

# Disable auto-start
sudo systemctl disable greenhouse-proxy
```

### Update Frontend Files

When you modify the ESP32's HTML files:

```bash
# From your development machine
./update-frontend.sh nkepah@100.92.151.67

# Or manually on the Pi
sudo cp /path/to/new/*.html /opt/greenhouse-proxy/public/
```

---

## 🔌 API Reference

### REST Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Proxy server status |
| `/api/config` | GET | Current configuration |
| `/api/config` | POST | Update configuration |
| `/api/weather` | GET | Cached weather data |
| `/esp32/*` | ANY | Proxy to ESP32 HTTP |

### WebSocket Protocol

Connect to `ws://pi-ip:3000/ws`

**Messages from server:**

```json
// Proxy status (sent on connect)
{
    "type": "proxy_status",
    "esp32Connected": true,
    "esp32Ip": "10.0.0.163",
    "proxyVersion": "2.0.0"
}

// Forwarded from ESP32 (sync, weather, etc.)
{
    "type": "sync",
    "devices": [...],
    "sensors": {...},
    "power": {...}
}
```

**Messages to server:**

All messages are forwarded to ESP32 except:

```json
// Handled by proxy
{ "type": "refresh_weather" }
{ "type": "get_proxy_status" }
```

---

## 🔧 Troubleshooting

### ESP32 not connecting

1. Check ESP32 IP is correct in config.json
2. Verify ESP32 is on the same network
3. Check ESP32 WebSocket is running on port 80

```bash
# Test ESP32 connectivity
curl http://10.0.0.163/
ping 10.0.0.163
```

### Frontend not loading

1. Check files exist in `/opt/greenhouse-proxy/public/`
2. Verify file permissions

```bash
ls -la /opt/greenhouse-proxy/public/
sudo chown -R nkepah:nkepah /opt/greenhouse-proxy/public/
```

### Service won't start

```bash
# Check detailed logs
sudo journalctl -u greenhouse-proxy -n 100

# Check Node.js version
node -v  # Should be 18+

# Manually test server
cd /opt/greenhouse-proxy
node server.js
```

### High memory usage

```bash
# Check process memory
ps aux | grep node

# Restart service (clears memory)
sudo systemctl restart greenhouse-proxy
```

---

## 📈 Performance Comparison

| Metric | Direct ESP32 | Via Pi Proxy |
|--------|--------------|--------------|
| Page load time | 3-5 seconds | < 100ms |
| WebSocket latency | Variable | Consistent |
| Max browser clients | 2-3 | Unlimited* |
| Weather fetch | 2-3s (SSL) | < 10ms (cached) |

*Limited by Pi RAM, typically 50+ clients easily

---

## 🔐 Security Notes

- The proxy runs on your local network only
- No authentication by default (add Nginx basic auth if needed)
- ESP32 communication is unencrypted (local network)
- For remote access, use VPN or SSH tunnel

### Adding Nginx with SSL

```bash
sudo ./install.sh --with-nginx --with-ssl
```

This creates a self-signed certificate. For trusted certificates, use Let's Encrypt:

```bash
sudo apt install certbot python3-certbot-nginx
sudo certbot --nginx -d your-domain.com
```

---

## 🧪 Development

### Local Development

```bash
cd greenhouse-proxy
npm install
npm run dev  # Uses nodemon for auto-reload
```

### Testing WebSocket

```javascript
// Browser console
const ws = new WebSocket('ws://localhost:3000/ws');
ws.onmessage = (e) => console.log(JSON.parse(e.data));
ws.send(JSON.stringify({ type: 'get_proxy_status' }));
```

---

## 📝 Changelog

### v2.0.0
- Complete rewrite with WebSocket multiplexer
- Frontend file serving
- Weather caching
- Systemd service integration
- One-line installer

### v1.0.0
- Initial release (device aggregation only)

---

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

---

## 📄 License

MIT License - See LICENSE file for details.

---

## 🆘 Support

- **Issues**: GitHub Issues
- **Email**: your-email@example.com
- **Documentation**: This README

---

Made with 🌱 for organic farming automation
