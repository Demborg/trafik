# Hallway Transit Sign

A low-power, glanceable e-ink transit dashboard for real-time SL subway and bus departures at Bagarmossen.

This repository contains the complete stack: the edge hardware firmware, the cloud proxy backend, and the configuration dashboard.

## 🏗 System Architecture

The system is designed around extreme battery efficiency at the edge, relying on a fast, stateless cloud proxy to do the heavy lifting.

* **Edge Hardware (`/firmware`):** An ESP32-C3 microcontroller (with onboard LiPo charger, chargeable via USB) driving a 4.26" Waveshare 800×480 black/white SPI e-ink display. Powered by a 3.7V 2000mAh LiPo battery (JST-PH). Two identical units are used — one for development, one deployed in the hallway.
* **API Proxy (`/backend`):** A minimalist Go service deployed to Google Cloud Run. It runs in a `distroless/static` container (includes CA certs for outbound HTTPS, nothing else) to guarantee sub-200ms cold starts. It fetches live departure data from the Trafiklab SL APIs, parses it, and serves a compact JSON payload to the ESP32. CORS is configured to allow requests from `trafik.demborg.se`.
* **Database:** Google Cloud Firestore (Native) is used as a serverless document store for managing device configurations and logging battery telemetry.
* **Dashboard (`/frontend`):** A lightweight Svelte frontend hosted on GitHub Pages at `trafik.demborg.se`. It talks directly to the backend to monitor battery health and configure target transit stations.

## ⏱ Polling & Display Strategy

Battery life is governed by two independent cycles:

1. **Network poll cycle (long):** The device wakes, connects to WiFi, fetches fresh departure data from the backend, updates the display, then returns to deep sleep. The backend response includes a recommended next-poll delay based on the next scheduled departure — if the next train is 20 minutes away, the device can safely sleep longer before the next fetch.
2. **Display refresh cycle (short):** Between network polls, the device wakes on a shorter timer to perform a local (partial) display refresh — updating a countdown or elapsed time using data already cached in RTC memory — without touching WiFi.

This separation keeps the display feeling live while minimising the expensive WiFi wake cycles that dominate battery draw.

> **Note on partial refresh:** The Waveshare 800×480 black/white display's support for partial refresh depends on firmware mode and panel revision. This will be validated once the hardware arrives.

## 📐 Design Philosophy

To keep this codebase highly maintainable and easy to reason about, we adhere to a few core principles across all three environments (C++, Go, and TypeScript):

1.  **Functional & Self-Documenting:** Code should read like a narrative. We prefer short, well-named, single-responsibility functions over complex inline logic or heavy comments.
2.  **Pure Data Transformations:** Especially in the backend, data flows through clear pipelines. Fetch data -> Transform data -> Serve data. Side effects (like writing to the database) are isolated.
3.  **No Infrastructure Magic:** We prioritize managed, serverless tools (Cloud Run, Firestore, GitHub Pages) over complex cluster management or heavy container orchestration. The focus is on the code, not the plumbing.

## 🚀 Getting Started

### 1. Backend (Go)
Navigate to the `/backend` directory. Ensure you have Go 1.21+ installed.
```bash
cd backend
go mod tidy
go run cmd/server/main.go
```

### 2. Firmware (C++)
The firmware is built using PlatformIO.

```bash
# Open the /firmware folder in VS Code with the PlatformIO extension installed.
# Copy the example config and fill in your credentials:
cp include/config.example.h include/config.h
# Edit config.h to add your Wi-Fi credentials, backend URL, and Trafiklab API key.
# Then build and flash via USB using the PlatformIO toolbar or:
pio run --target upload
```

### 3. Frontend (Svelte)
Navigate to the `/frontend` directory. Ensure you have Node.js installed.
```bash
cd frontend
npm install
npm run dev
```
