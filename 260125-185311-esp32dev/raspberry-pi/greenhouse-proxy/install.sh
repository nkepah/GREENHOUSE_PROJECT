#!/bin/bash
#
# 🌿 Greenhouse Proxy - Complete Installation Script
#
# This script installs and configures the Greenhouse Proxy server on Raspberry Pi.
# It handles:
#   - Node.js installation (if needed)
#   - npm dependencies
#   - Systemd service setup
#   - Nginx reverse proxy (optional)
#   - Frontend file deployment
#
# Usage:
#   chmod +x install.sh
#   sudo ./install.sh
#
# Options:
#   --esp32-ip=10.0.0.163    Set ESP32 IP address
#   --with-nginx                Install Nginx reverse proxy
#   --with-ssl                  Enable self-signed SSL (requires --with-nginx)
#   --port=3000                 Set server port
#

set -e  # Exit on error

# =============================================================================
# CONFIGURATION
# =============================================================================

INSTALL_DIR="/opt/greenhouse-proxy"
SERVICE_NAME="greenhouse-proxy"
DEFAULT_PORT=3000
DEFAULT_ESP32_IP="10.0.0.163"
DEFAULT_PI_USER="nkepah"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# =============================================================================
# HELPER FUNCTIONS
# =============================================================================

print_banner() {
    echo -e "${GREEN}"
    echo "╔════════════════════════════════════════════════════════════════╗"
    echo "║                                                                ║"
    echo "║   🌿 GREENHOUSE PROXY INSTALLER                                ║"
    echo "║                                                                ║"
    echo "║   Raspberry Pi WebSocket Proxy for ESP32 Greenhouse            ║"
    echo "║                                                                ║"
    echo "╚════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "Please run as root (sudo ./install.sh)"
        exit 1
    fi
}

# =============================================================================
# PARSE ARGUMENTS
# =============================================================================

ESP32_IP=$DEFAULT_ESP32_IP
PORT=$DEFAULT_PORT
WITH_NGINX=false
WITH_SSL=false

for arg in "$@"; do
    case $arg in
        --esp32-ip=*)
            ESP32_IP="${arg#*=}"
            ;;
        --port=*)
            PORT="${arg#*=}"
            ;;
        --with-nginx)
            WITH_NGINX=true
            ;;
        --with-ssl)
            WITH_SSL=true
            ;;
        --help)
            echo "Usage: sudo ./install.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --esp32-ip=IP      ESP32 IP address (default: 10.0.0.163)"
            echo "  --port=PORT        Server port (default: 3000)"
            echo "  --with-nginx       Install Nginx reverse proxy"
            echo "  --with-ssl         Enable self-signed SSL certificate"
            echo ""
            exit 0
            ;;
    esac
done

# =============================================================================
# MAIN INSTALLATION
# =============================================================================

print_banner
check_root

log_info "Installation configuration:"
echo "  - Install directory: $INSTALL_DIR"
echo "  - ESP32 IP: $ESP32_IP"
echo "  - Server port: $PORT"
echo "  - Nginx: $WITH_NGINX"
echo "  - SSL: $WITH_SSL"
echo ""

# Auto-confirm (bypass prompt)
log_info "Auto-confirming installation..."

# -----------------------------------------------------------------------------
# Step 1: System Update
# -----------------------------------------------------------------------------

log_info "Updating system packages..."
apt-get update -qq -y
apt-get upgrade -y -qq
log_success "System updated"

# -----------------------------------------------------------------------------
# Step 2: Install Node.js
# -----------------------------------------------------------------------------

log_info "Checking Node.js..."

if command -v node &> /dev/null; then
    NODE_VERSION=$(node -v)
    log_success "Node.js already installed: $NODE_VERSION"
else
    log_info "Installing Node.js 20.x..."
    curl -fsSL https://deb.nodesource.com/setup_20.x | bash -
    DEBIAN_FRONTEND=noninteractive apt-get install -y nodejs
    log_success "Node.js installed: $(node -v)"
fi

# Verify npm
if ! command -v npm &> /dev/null; then
    log_error "npm not found. Please install Node.js manually."
    exit 1
fi

# -----------------------------------------------------------------------------
# Step 3: Create Installation Directory
# -----------------------------------------------------------------------------

log_info "Creating installation directory..."

if [ -d "$INSTALL_DIR" ]; then
    log_warn "Directory exists. Backing up config..."
    if [ -f "$INSTALL_DIR/config.json" ]; then
        cp "$INSTALL_DIR/config.json" "/tmp/greenhouse-proxy-config-backup.json"
    fi
fi

mkdir -p "$INSTALL_DIR"
mkdir -p "$INSTALL_DIR/public"

# -----------------------------------------------------------------------------
# Step 4: Copy Files
# -----------------------------------------------------------------------------

log_info "Copying server files..."

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cp "$SCRIPT_DIR/server.js" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/package.json" "$INSTALL_DIR/"
cp "$SCRIPT_DIR/greenhouse-proxy.service" "$INSTALL_DIR/"

# Restore config backup or create new one
if [ -f "/tmp/greenhouse-proxy-config-backup.json" ]; then
    cp "/tmp/greenhouse-proxy-config-backup.json" "$INSTALL_DIR/config.json"
    log_info "Restored previous configuration"
else
    # Create config with specified ESP32 IP
    cat > "$INSTALL_DIR/config.json" << EOF
{
    "port": $PORT,
    "esp32": {
        "ip": "$ESP32_IP",
        "wsPort": 80,
        "httpPort": 80,
        "reconnectInterval": 5000,
        "pingInterval": 30000,
        "name": "Greenhouse"
    },
    "weather": {
        "enabled": true,
        "cacheMinutes": 10,
        "location": { "lat": "-17.8292", "lon": "31.0522" },
        "timezone": "Africa/Harare",
        "unit": "celsius"
    },
    "logging": {
        "level": "info",
        "timestamps": true
    }
}
EOF
fi

log_success "Files copied"

# -----------------------------------------------------------------------------
# Step 5: Copy Frontend Files
# -----------------------------------------------------------------------------

log_info "Copying frontend files..."

# Check if frontend files exist in the script directory
if [ -d "$SCRIPT_DIR/public" ]; then
    cp -r "$SCRIPT_DIR/public/"* "$INSTALL_DIR/public/" 2>/dev/null || true
    log_success "Frontend files copied from local directory"
elif [ -d "$SCRIPT_DIR/../data" ]; then
    # Copy from ESP32 data folder
    cp "$SCRIPT_DIR/../data/"*.html "$INSTALL_DIR/public/" 2>/dev/null || true
    log_success "Frontend files copied from ESP32 data folder"
else
    log_warn "No frontend files found. Copy manually to $INSTALL_DIR/public/"
fi

# Decompress .gz files if present
for gz_file in "$INSTALL_DIR/public/"*.gz; do
    if [ -f "$gz_file" ]; then
        gunzip -kf "$gz_file" 2>/dev/null || true
    fi
done

# -----------------------------------------------------------------------------
# Step 6: Install npm Dependencies
# -----------------------------------------------------------------------------

log_info "Installing npm dependencies..."
cd "$INSTALL_DIR"
npm install --production --silent
log_success "Dependencies installed"

# -----------------------------------------------------------------------------
# Step 7: Set Permissions
# -----------------------------------------------------------------------------

log_info "Setting permissions..."
chown -R nkepah:nkepah "$INSTALL_DIR"
chmod +x "$INSTALL_DIR/server.js"
log_success "Permissions set"

# -----------------------------------------------------------------------------
# Step 8: Install Systemd Service
# -----------------------------------------------------------------------------

log_info "Installing systemd service..."

cp "$INSTALL_DIR/greenhouse-proxy.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable greenhouse-proxy
log_success "Systemd service installed"

# -----------------------------------------------------------------------------
# Step 9: Nginx (Optional)
# -----------------------------------------------------------------------------

if [ "$WITH_NGINX" = true ]; then
    log_info "Installing Nginx..."
    DEBIAN_FRONTEND=noninteractive apt-get install -y nginx
    
    # Create Nginx config
    cat > /etc/nginx/sites-available/greenhouse << EOF
server {
    listen 80;
    server_name _;
    
    location / {
        proxy_pass http://127.0.0.1:$PORT;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_read_timeout 86400;
    }
}
EOF

    # Enable site
    ln -sf /etc/nginx/sites-available/greenhouse /etc/nginx/sites-enabled/
    rm -f /etc/nginx/sites-enabled/default
    
    # SSL (Optional)
    if [ "$WITH_SSL" = true ]; then
        log_info "Generating self-signed SSL certificate..."
        mkdir -p /etc/nginx/ssl
        openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
            -keyout /etc/nginx/ssl/greenhouse.key \
            -out /etc/nginx/ssl/greenhouse.crt \
            -subj "/C=ZW/ST=Harare/L=Harare/O=Farm/CN=greenhouse.local"
        
        # Update Nginx config for SSL
        cat > /etc/nginx/sites-available/greenhouse << EOF
server {
    listen 80;
    server_name _;
    return 301 https://\$host\$request_uri;
}

server {
    listen 443 ssl http2;
    server_name _;
    
    ssl_certificate /etc/nginx/ssl/greenhouse.crt;
    ssl_certificate_key /etc/nginx/ssl/greenhouse.key;
    
    location / {
        proxy_pass http://127.0.0.1:$PORT;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_read_timeout 86400;
    }
}
EOF
        log_success "SSL configured"
    fi
    
    nginx -t && systemctl restart nginx
    systemctl enable nginx
    log_success "Nginx configured"
fi

# -----------------------------------------------------------------------------
# Step 10: Start Service
# -----------------------------------------------------------------------------

log_info "Starting greenhouse-proxy service..."
systemctl start greenhouse-proxy

# Wait a moment for service to start
sleep 2

if systemctl is-active --quiet greenhouse-proxy; then
    log_success "Service started successfully"
else
    log_error "Service failed to start. Check logs: journalctl -u greenhouse-proxy"
fi

# -----------------------------------------------------------------------------
# Complete
# -----------------------------------------------------------------------------

PI_IP=$(hostname -I | awk '{print $1}')

echo ""
echo -e "${GREEN}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                                                                ║${NC}"
echo -e "${GREEN}║   ✅ INSTALLATION COMPLETE                                     ║${NC}"
echo -e "${GREEN}║                                                                ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Access your greenhouse dashboard at:"
echo ""
if [ "$WITH_NGINX" = true ]; then
    if [ "$WITH_SSL" = true ]; then
        echo "  🔒 https://$PI_IP/"
    else
        echo "  🌐 http://$PI_IP/"
    fi
else
    echo "  🌐 http://$PI_IP:$PORT/"
fi
echo ""
echo "Useful commands:"
echo "  View logs:      sudo journalctl -u greenhouse-proxy -f"
echo "  Restart:        sudo systemctl restart greenhouse-proxy"
echo "  Stop:           sudo systemctl stop greenhouse-proxy"
echo "  Edit config:    sudo nano $INSTALL_DIR/config.json"
echo ""
echo "ESP32 IP configured: $ESP32_IP"
echo "To change: Edit $INSTALL_DIR/config.json and restart service"
echo ""
