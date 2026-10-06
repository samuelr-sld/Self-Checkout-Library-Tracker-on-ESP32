// Copy this file to "secrets.h" (same folder) and fill in your own values.
// secrets.h is gitignored so your credentials never get committed.
#pragma once

// WiFi network the kiosk joins
#define WIFI_SSID     "your-wifi-name"
#define WIFI_PASSWORD "your-wifi-password"

// The kiosk's own Firebase Authentication account (Email/Password provider).
// Create it in the Firebase console, then add its UID to the `devices` collection
// (see README). Use a dedicated account, not your admin login.
#define DEVICE_EMAIL    "kiosk@example.com"
#define DEVICE_PASSWORD "your-device-password"
