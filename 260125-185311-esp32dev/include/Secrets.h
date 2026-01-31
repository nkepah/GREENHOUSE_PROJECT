#ifndef SECRETS_H
#define SECRETS_H

// WiFi credentials - can be overridden by provisioning
#define DEFAULT_SSID "VirginiaFarm"
#define DEFAULT_PASS "GreenThumb2024!"

// NTP Server
#define NTP_SERVER "pool.ntp.org"

// Time zone settings (Spotsylvania, VA - EST/EDT)
#define GMT_OFFSET_SECONDS (-5 * 3600)       // EST
#define DAYLIGHT_OFFSET_SECONDS (3600)       // EDT (add 1 hour)

// Device identification
#define DEVICE_HOSTNAME "greenhouse"
#define DEVICE_TYPE_GREENHOUSE "greenhouse"
#define DEVICE_TYPE_COOP "coop"

#endif
