#pragma once

// Wi-Fi credentials
#define WIFI_SSID     "your_ssid_here"
#define WIFI_PASSWORD "your_password_here"

// URL of the deployed Go backend
#define BACKEND_URL "https://your-cloud-run-url/departures"

// How many display-refresh wake cycles between full network polls.
// e.g. 3 = poll network, then refresh display twice, then poll again.
#define NETWORK_POLL_EVERY_N_CYCLES 3

// Fallback sleep duration if the network poll fails (seconds)
#define POLL_FALLBACK_SLEEP_SECONDS 120

// Sleep between display-only refresh cycles (seconds)
#define DISPLAY_REFRESH_SLEEP_SECONDS 30
