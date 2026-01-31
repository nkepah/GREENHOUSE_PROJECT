#!/bin/bash

# Deploy fresh code and restart service
echo "🚀 Deploying to Raspberry Pi..."

# Push files to Pi
scp server.js nkepah@100.92.151.67:/opt/greenhouse-proxy/server.js
scp database.js nkepah@100.92.151.67:/opt/greenhouse-proxy/database.js
scp dashboard/index.html nkepah@100.92.151.67:/opt/greenhouse-proxy/dashboard/index.html

echo "📁 Files deployed. Clearing old database and restarting..."

# SSH to Pi and clear DB + restart
ssh nkepah@100.92.151.67 << 'EOF'
sudo rm -f /opt/greenhouse-proxy/farm.db
echo "✅ Old database cleared"
sudo systemctl restart greenhouse-proxy
echo "✅ Service restarted"
sleep 2
sudo systemctl status greenhouse-proxy --no-pager
EOF

echo "✅ Deployment complete!"
