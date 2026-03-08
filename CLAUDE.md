# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Hallway transit sign monorepo — an e-ink departure board for SL (Stockholm transit) at Bagarmossen. Three components: ESP32-C3 firmware, Go API proxy, Svelte dashboard.

## Task Runner

All common tasks use `mask` (reads `maskfile.md`). Run from the project root. See `maskfile.md` for available commands.

Use the `trafik-tasks` skill for task runner operations.

## Architecture

### Backend (`/backend`) — Go 1.26
- `cmd/server/main.go`: HTTP server setup, CORS middleware, env config
- `internal/sl/client.go`: Trafiklab SL Transport API client (Bagarmossen site ID `9192`)
- `internal/handlers/departures.go`: Fetches departures, converts to `minutes` until departure, computes `suggested_sleep_seconds` for the ESP32

Single endpoint: `GET /departures` returns `{departures: [...], suggested_sleep_seconds: N}`

Env vars: `ALLOWED_ORIGIN` (default `*`), `PORT` (default `8080`)

### Frontend (`/frontend`) — Svelte 5
- Single-page dashboard at `trafik.demborg.se` (GitHub Pages)
- Fetches `GET /departures` from the backend on mount
- Uses Svelte 5 runes (`$state`)
- Configured via `VITE_BACKEND_URL` env var

### Firmware (`/firmware`) — C++ / PlatformIO (ESP32-C3)
- All logic runs in `setup()` before deep sleep — `loop()` is never reached
- Two wake cycles: **network cycle** (WiFi + backend fetch) and **display-only cycle** (refresh from RTC-cached payload)
- `bootCount` and `cachedPayload[512]` stored in RTC memory (survive deep sleep)
- `NETWORK_POLL_EVERY_N_CYCLES` controls how often WiFi is used
- Copy `include/config.example.h` → `include/config.h` before building

## Deployment

- Backend: Google Cloud Run (`europe-north2`), `distroless/static` container. CI deploys on push to `main`. Live at `https://trafik-backend-489043292627.europe-north2.run.app`.
- Frontend: GitHub Pages, CI builds and deploys on push to `main`.
- Database: Google Cloud Firestore (device config + battery telemetry) — not yet wired into the code.
