#pragma once

// Wi-Fi credentials
#define WIFI_SSID     "your_ssid_here"
#define WIFI_PASSWORD "your_password_here"

// URL of the deployed Go backend
#define BACKEND_URL "https://your-cloud-run-url/departures"

// E-ink display SPI pins (Waveshare 4.26" 800x480, GxEPD2_426_GDEQ0426T82)
// Wired to ESP32-C3 SPI2 (FSPI) defaults.
#define EPD_SCK   6
#define EPD_MOSI  7
#define EPD_CS    10
#define EPD_DC    8
#define EPD_RST   9
#define EPD_BUSY  5
#define EPD_PWR   4

// How many display-refresh wake cycles between full network polls.
// e.g. 3 = poll network, then refresh display twice, then poll again.
#define NETWORK_POLL_EVERY_N_CYCLES 3

// Fallback sleep duration if the network poll fails (seconds)
#define POLL_FALLBACK_SLEEP_SECONDS 120

// Sleep between display-only refresh cycles (seconds).
// Keep at 60 so each display wake corresponds to exactly one elapsed minute.
#define DISPLAY_REFRESH_SLEEP_SECONDS 60
