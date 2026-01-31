#!/usr/bin/env python3
"""
🌾 Farm Hub - Complete Raspberry Pi Setup
Automated installation script with interactive configuration

Usage:
    sudo python3 setup_complete.py

This script provides:
- Full system setup with nginx, SSL, and backend services
- Interactive ESP32 IP configuration
- Unified dashboard deployment
- Service management
"""

import os
import sys
import json
import subprocess
import shutil
from pathlib import Path
from typing import Dict, Optional
import socket
import time

# ═══════════════════════════════════════════════════════════════════════════════
# Configuration
# ═══════════════════════════════════════════════════════════════════════════════

INSTALL_DIR = Path("/opt/farmhub")
WEB_DIR = Path("/var/www/farmhub")
CONFIG_DIR = Path("/etc/farmhub")
LOG_DIR = Path("/var/log/farmhub")

# Colors for terminal output
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    BLUE = '\033[0;34m'
    CYAN = '\033[0;36m'
    MAGENTA = '\033[0;35m'
    NC = '\033[0m'  # No Color

def print_banner():
    """Print the Farm Hub banner"""
    print(f"{Colors.CYAN}")
    print(r"""
  _____ _    ____  __  __   _   _ _   _ ____  
 |  ___/ \  |  _ \|  \/  | | | | | | | | __ ) 
 | |_ / _ \ | |_) | |\/| | | |_| | | | |  _ \ 
 |  _/ ___ \|  _ <| |  | | |  _  | |_| | |_) |
 |_|/_/   \_\_| \_\_|  |_| |_| |_|\___/|____/ 
                                              
  🌾 Organic Farm IoT Hub - Complete Setup 🌾
    """)
    print(f"{Colors.NC}")

def print_step(step: int, total: int, message: str):
    """Print a step header"""
    print(f"\n{Colors.BLUE}[{step}/{total}] {message}{Colors.NC}")

def print_success(message: str):
    """Print success message"""
    print(f"{Colors.GREEN}✓ {message}{Colors.NC}")

def print_warning(message: str):
    """Print warning message"""
    print(f"{Colors.YELLOW}⚠ {message}{Colors.NC}")

def print_error(message: str):
    """Print error message"""
    print(f"{Colors.RED}✗ {message}{Colors.NC}")

def run_command(cmd: str, check: bool = True) -> subprocess.CompletedProcess:
    """Run a shell command"""
    return subprocess.run(cmd, shell=True, check=check, capture_output=True, text=True)

def is_root() -> bool:
    """Check if running as root"""
    return os.geteuid() == 0

def get_local_ip() -> str:
    """Get the local IP address"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except:
        return "127.0.0.1"

def check_ip_reachable(ip: str, port: int = 80, timeout: float = 2.0) -> bool:
    """Check if an IP:port is reachable"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((ip, port))
        sock.close()
        return result == 0
    except:
        return False

def suggest_farm_names() -> list:
    """Return list of suggested farm names"""
    return [
        "1. 🌾 Verdant Valley Farms",
        "2. 🌿 Greenfield Homestead", 
        "3. 🌻 Sunrise Organic Ranch",
        "4. 🍃 Heritage Pastures",
        "5. 🌱 Meadow Creek Farm",
        "6. 🐄 Rolling Hills Ranch",
        "7. 🌳 Oakwood Organics",
        "8. 🌾 Golden Harvest Homestead",
        "9. 🐔 Freedom Range Farm",
        "10. 🌿 Wildflower Meadows"
    ]

# ═══════════════════════════════════════════════════════════════════════════════
# Interactive Configuration
# ═══════════════════════════════════════════════════════════════════════════════

def interactive_config() -> Dict:
    """Interactive configuration wizard"""
    config = {
        "farm_name": "",
        "domain": "farm.local",
        "devices": {},
        "weather": {
            "cache_minutes": 10,
            "latitude": "",
            "longitude": ""
        },
        "ssl": {
            "enabled": True,
            "self_signed": True
        }
    }
    
    print(f"\n{Colors.CYAN}═══════════════════════════════════════════════════════════════{Colors.NC}")
    print(f"{Colors.CYAN}           Farm Hub Configuration Wizard{Colors.NC}")
    print(f"{Colors.CYAN}═══════════════════════════════════════════════════════════════{Colors.NC}\n")
    
    # Farm Name
    print(f"{Colors.YELLOW}Suggested Farm Names for your organic operation:{Colors.NC}\n")
    for name in suggest_farm_names():
        print(f"  {name}")
    
    print(f"\n{Colors.GREEN}Your farm features:{Colors.NC}")
    print("  🐄 Grass-fed Beef Cattle")
    print("  🐔 Free-range Chickens & Farm Fresh Eggs")
    print("  🐐 Grass-fed Goats")
    print("  🌱 Organic Vegetables\n")
    
    farm_name = input(f"{Colors.CYAN}Enter your farm name (or number 1-10): {Colors.NC}").strip()
    if farm_name.isdigit() and 1 <= int(farm_name) <= 10:
        names = [
            "Verdant Valley Farms", "Greenfield Homestead", "Sunrise Organic Ranch",
            "Heritage Pastures", "Meadow Creek Farm", "Rolling Hills Ranch",
            "Oakwood Organics", "Golden Harvest Homestead", "Freedom Range Farm",
            "Wildflower Meadows"
        ]
        farm_name = names[int(farm_name) - 1]
    config["farm_name"] = farm_name or "Farm Hub"
    
    # Domain
    domain = input(f"{Colors.CYAN}Enter domain name [{config['domain']}]: {Colors.NC}").strip()
    config["domain"] = domain or config["domain"]
    
    # Weather location
    print(f"\n{Colors.YELLOW}Weather Configuration:{Colors.NC}")
    lat = input(f"{Colors.CYAN}Enter latitude (e.g., 37.7749): {Colors.NC}").strip()
    lon = input(f"{Colors.CYAN}Enter longitude (e.g., -122.4194): {Colors.NC}").strip()
    config["weather"]["latitude"] = lat
    config["weather"]["longitude"] = lon
    
    # Device Configuration
    print(f"\n{Colors.YELLOW}ESP32 Device Configuration:{Colors.NC}")
    print("Configure the IP addresses of your ESP32 devices.\n")
    
    devices = {
        "greenhouse": {
            "name": "Greenhouse",
            "icon": "🌱",
            "type": "greenhouse",
            "description": "Main greenhouse - vegetables & seedlings"
        },
        "coop1": {
            "name": "Coop Alpha",
            "icon": "🐔",
            "type": "chicken_coop", 
            "description": "Layer hens - egg production"
        },
        "coop2": {
            "name": "Coop Beta",
            "icon": "🐓",
            "type": "chicken_coop",
            "description": "Broilers - meat production"
        },
        "coop3": {
            "name": "Coop Gamma",
            "icon": "🐣",
            "type": "chicken_coop",
            "description": "Nursery - chicks & young birds"
        }
    }
    
    for device_id, device_info in devices.items():
        print(f"\n{device_info['icon']} {Colors.GREEN}{device_info['name']}{Colors.NC} ({device_info['description']})")
        ip = input(f"  IP Address [{device_info['default_ip']}]: ").strip()
        ip = ip or device_info['default_ip']
        
        # Check if reachable
        print(f"  Checking {ip}...", end=" ", flush=True)
        if check_ip_reachable(ip):
            print(f"{Colors.GREEN}✓ Online{Colors.NC}")
        else:
            print(f"{Colors.YELLOW}✗ Offline (will configure anyway){Colors.NC}")
        
        config["devices"][device_id] = {
            "name": device_info["name"],
            "ip": ip,
            "port": 80,
            "type": device_info["type"],
            "icon": device_info["icon"],
            "description": device_info["description"]
        }
    
    return config

# ═══════════════════════════════════════════════════════════════════════════════
# Installation Functions
# ═══════════════════════════════════════════════════════════════════════════════

def install_packages():
    """Install required system packages"""
    packages = [
        "nginx",
        "certbot",
        "python3-certbot-nginx",
        "python3-pip",
        "python3-venv",
        "nodejs",
        "npm",
        "avahi-daemon",
        "sqlite3",
        "git",
        "curl",
        "jq",
        "openssl"
    ]
    
    print("Updating package lists...")
    run_command("apt update")
    
    print("Installing packages...")
    run_command(f"apt install -y {' '.join(packages)}")

def create_directories():
    """Create required directories"""
    for directory in [INSTALL_DIR, WEB_DIR, CONFIG_DIR, LOG_DIR]:
        directory.mkdir(parents=True, exist_ok=True)
    
    # Set permissions
    shutil.chown(WEB_DIR, "www-data", "www-data")

def generate_ssl_certificate(config: Dict):
    """Generate self-signed SSL certificate"""
    ssl_dir = Path("/etc/nginx/ssl")
    ssl_dir.mkdir(parents=True, exist_ok=True)
    
    cert_path = ssl_dir / "farmhub.crt"
    key_path = ssl_dir / "farmhub.key"
    
    if cert_path.exists() and key_path.exists():
        print_warning("SSL certificate already exists, skipping generation")
        return
    
    farm_name = config.get("farm_name", "Farm Hub")
    domain = config.get("domain", "farm.local")
    
    cmd = f"""openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
        -keyout {key_path} \
        -out {cert_path} \
        -subj "/C=US/ST=Virginia/L=Farm/O={farm_name}/CN={domain}" """
    
    run_command(cmd)

def configure_nginx(config: Dict):
    """Configure nginx reverse proxy"""
    devices = config.get("devices", {})
    
    # Build upstream blocks
    upstreams = ""
    locations = ""
    
    for device_id, device in devices.items():
        ip = device.get("ip", "127.0.0.1")
        port = device.get("port", 80)
        
        upstreams += f"""
upstream {device_id} {{
    server {ip}:{port};
    keepalive 2;
}}
"""
        
        locations += f"""
    # ==================== {device.get('name', device_id).upper()} ====================
    location /{device_id}/ {{
        proxy_pass http://{device_id}/;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 5s;
        proxy_read_timeout 60s;
    }}

    location /{device_id}/ws {{
        proxy_pass http://{device_id}/ws;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 86400;
    }}
"""
    
    nginx_config = f"""# Farm Hub - Unified IoT Dashboard
# Auto-generated configuration

# Rate limiting
limit_req_zone $binary_remote_addr zone=api_limit:10m rate=10r/s;
limit_req_zone $binary_remote_addr zone=ws_limit:10m rate=5r/s;

# Upstream definitions
{upstreams}

# HTTP -> HTTPS redirect
server {{
    listen 80;
    server_name _;
    return 301 https://$host$request_uri;
}}

# Main HTTPS server
server {{
    listen 443 ssl http2;
    server_name _;

    # SSL Configuration
    ssl_certificate /etc/nginx/ssl/farmhub.crt;
    ssl_certificate_key /etc/nginx/ssl/farmhub.key;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256;
    ssl_prefer_server_ciphers off;
    ssl_session_cache shared:SSL:10m;

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;

    # Gzip
    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    # Root for dashboard
    root /var/www/farmhub;
    index index.html;

    # Main dashboard
    location / {{
        try_files $uri $uri/ /index.html;
    }}

    # API endpoints (Pi backend)
    location /api/ {{
        limit_req zone=api_limit burst=10 nodelay;
        proxy_pass http://127.0.0.1:3000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }}

    {locations}

    # Error pages
    error_page 502 503 504 /error.html;
    location = /error.html {{
        internal;
        root /var/www/farmhub;
    }}
}}
"""
    
    nginx_path = Path("/etc/nginx/sites-available/farmhub")
    nginx_path.write_text(nginx_config)
    
    # Enable site
    enabled_path = Path("/etc/nginx/sites-enabled/farmhub")
    if enabled_path.exists():
        enabled_path.unlink()
    enabled_path.symlink_to(nginx_path)
    
    # Remove default site
    default_site = Path("/etc/nginx/sites-enabled/default")
    if default_site.exists():
        default_site.unlink()
    
    # Test and reload
    run_command("nginx -t")
    run_command("systemctl reload nginx")

def setup_backend(config: Dict):
    """Setup Node.js backend service"""
    backend_dir = INSTALL_DIR / "backend"
    backend_dir.mkdir(parents=True, exist_ok=True)
    
    # package.json
    package_json = {
        "name": "farmhub-backend",
        "version": "1.0.0",
        "main": "server.js",
        "scripts": {
            "start": "node server.js"
        },
        "dependencies": {
            "express": "^4.18.2",
            "node-fetch": "^2.7.0",
            "node-cache": "^5.1.2"
        }
    }
    
    (backend_dir / "package.json").write_text(json.dumps(package_json, indent=2))
    
    # Server code
    server_js = '''
const express = require('express');
const fetch = require('node-fetch');
const NodeCache = require('node-cache');
const fs = require('fs');

const app = express();
const PORT = 3000;

// Load config
let config = {};
try {
    config = JSON.parse(fs.readFileSync('/etc/farmhub/config.json', 'utf8'));
} catch (e) {
    console.error('Config load error:', e.message);
}

// Caches
const weatherCache = new NodeCache({ stdTTL: (config.weather?.cache_minutes || 10) * 60 });
const deviceCache = new NodeCache({ stdTTL: 30 });

app.use(express.json());
app.use((req, res, next) => {
    res.header('Access-Control-Allow-Origin', '*');
    next();
});

// Weather endpoint
app.get('/api/weather', async (req, res) => {
    const lat = req.query.lat || config.weather?.latitude;
    const lon = req.query.lon || config.weather?.longitude;
    
    if (!lat || !lon) return res.status(400).json({ error: 'Missing coordinates' });
    
    const cacheKey = `weather_${lat}_${lon}`;
    let data = weatherCache.get(cacheKey);
    
    if (!data) {
        try {
            const url = `https://api.open-meteo.com/v1/forecast?latitude=${lat}&longitude=${lon}` +
                `&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,is_day,apparent_temperature` +
                `&hourly=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min` +
                `&forecast_days=1&temperature_unit=celsius&wind_speed_unit=kmh&timezone=auto`;
            
            const response = await fetch(url);
            data = await response.json();
            weatherCache.set(cacheKey, data);
        } catch (error) {
            return res.status(500).json({ error: 'Weather fetch failed' });
        }
    }
    res.json(data);
});

// Device status
app.get('/api/devices', async (req, res) => {
    const devices = config.devices || {};
    const results = {};
    
    await Promise.all(Object.entries(devices).map(async ([id, device]) => {
        try {
            const controller = new AbortController();
            const timeout = setTimeout(() => controller.abort(), 3000);
            const response = await fetch(`http://${device.ip}:${device.port}/api/status`, { signal: controller.signal });
            clearTimeout(timeout);
            const status = await response.json();
            results[id] = { ...device, status: { ...status, online: true } };
        } catch {
            results[id] = { ...device, status: { online: false } };
        }
    }));
    
    res.json(results);
});

// Farm info
app.get('/api/farm', (req, res) => {
    res.json({
        name: config.farm_name,
        domain: config.domain,
        devices: Object.keys(config.devices || {}).length,
        features: [
            "🐄 Grass-fed Beef Cattle",
            "🐔 Free-range Chickens & Farm Fresh Eggs",
            "🐐 Grass-fed Goats",
            "🌱 Organic Vegetables"
        ]
    });
});

app.get('/api/health', (req, res) => res.json({ status: 'ok', uptime: process.uptime() }));

app.listen(PORT, '127.0.0.1', () => console.log(`🌾 Farm Hub Backend on port ${PORT}`));
'''
    
    (backend_dir / "server.js").write_text(server_js)
    
    # Install dependencies
    os.chdir(backend_dir)
    run_command("npm install")
    
    # Create systemd service
    service = f"""[Unit]
Description=Farm Hub Backend API
After=network.target

[Service]
Type=simple
User=root
WorkingDirectory={backend_dir}
ExecStart=/usr/bin/node server.js
Restart=always
RestartSec=10
Environment=NODE_ENV=production

[Install]
WantedBy=multi-user.target
"""
    
    Path("/etc/systemd/system/farmhub-backend.service").write_text(service)
    run_command("systemctl daemon-reload")
    run_command("systemctl enable farmhub-backend")
    run_command("systemctl start farmhub-backend")

def deploy_dashboard():
    """Deploy the unified dashboard"""
    script_dir = Path(__file__).parent
    dashboard_src = script_dir / "dashboard"
    
    if dashboard_src.exists():
        for file in dashboard_src.glob("*"):
            dest = WEB_DIR / file.name
            if file.is_file():
                shutil.copy2(file, dest)
        print_success("Dashboard deployed from local files")
    else:
        print_warning("Dashboard source not found, creating placeholder")
        index = WEB_DIR / "index.html"
        index.write_text("<h1>Farm Hub - Dashboard files not found</h1>")

def save_config(config: Dict):
    """Save configuration to file"""
    config_path = CONFIG_DIR / "config.json"
    config_path.write_text(json.dumps(config, indent=2))
    print_success(f"Configuration saved to {config_path}")

# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

def main():
    print_banner()
    
    if not is_root():
        print_error("This script must be run as root (use sudo)")
        sys.exit(1)
    
    local_ip = get_local_ip()
    print(f"Raspberry Pi IP: {Colors.GREEN}{local_ip}{Colors.NC}\n")
    
    # Interactive configuration
    config = interactive_config()
    
    print(f"\n{Colors.CYAN}Starting installation...{Colors.NC}\n")
    
    total_steps = 7
    
    try:
        print_step(1, total_steps, "Installing system packages")
        install_packages()
        print_success("Packages installed")
        
        print_step(2, total_steps, "Creating directories")
        create_directories()
        print_success("Directories created")
        
        print_step(3, total_steps, "Saving configuration")
        save_config(config)
        
        print_step(4, total_steps, "Generating SSL certificate")
        generate_ssl_certificate(config)
        print_success("SSL certificate generated")
        
        print_step(5, total_steps, "Configuring Nginx")
        configure_nginx(config)
        print_success("Nginx configured")
        
        print_step(6, total_steps, "Setting up backend service")
        setup_backend(config)
        print_success("Backend service started")
        
        print_step(7, total_steps, "Deploying dashboard")
        deploy_dashboard()
        print_success("Dashboard deployed")
        
    except Exception as e:
        print_error(f"Installation failed: {e}")
        sys.exit(1)
    
    # Final summary
    print(f"\n{Colors.CYAN}{'═' * 65}{Colors.NC}")
    print(f"{Colors.GREEN}        🌾 FARM HUB INSTALLATION COMPLETE! 🌾{Colors.NC}")
    print(f"{Colors.CYAN}{'═' * 65}{Colors.NC}")
    print(f"""
  Farm Name: {Colors.YELLOW}{config['farm_name']}{Colors.NC}
  
  {Colors.GREEN}Access Points:{Colors.NC}
    • Dashboard:  https://{local_ip}/
    • Greenhouse: https://{local_ip}/greenhouse/
    • Coop Alpha: https://{local_ip}/coop1/
    • Coop Beta:  https://{local_ip}/coop2/
    • Coop Gamma: https://{local_ip}/coop3/
  
  {Colors.YELLOW}Note:{Colors.NC} Accept the self-signed certificate warning in your browser.
  For a trusted certificate, run: sudo certbot --nginx
  
  {Colors.GREEN}Services:{Colors.NC}
    • Check nginx:  sudo systemctl status nginx
    • Check backend: sudo systemctl status farmhub-backend
    • View logs:    sudo journalctl -u farmhub-backend -f
  
  {Colors.CYAN}Configuration:{Colors.NC} /etc/farmhub/config.json
""")

if __name__ == "__main__":
    main()
