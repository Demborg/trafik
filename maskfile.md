# Trafik

> Task runner for the trafik monorepo. Run `mask <command> <subcommand>` from the project root.
> Requires: mask, go, node/npm, platformio (pio), docker, gcloud

## dev

> Start both backend and frontend for local development.

```bash
mask backend dev & mask frontend dev & wait
```

## backend

> Commands for the Go backend service.

### backend dev

> Start the backend server locally.

```bash
cd backend
go run ./cmd/server
```

### backend test

> Run all backend unit tests.

```bash
cd backend
go test ./...
```

### backend build

> Build the backend Docker image locally.

```bash
docker build -t trafik-backend ./backend
```

### backend deploy

> Manually deploy the backend to Cloud Run (normally handled by GHA on push to main).

```bash
gcloud run deploy trafik-backend \
  --source ./backend \
  --region europe-north1 \
  --allow-unauthenticated
```

### backend logs

> Tail live logs from the Cloud Run backend.

```bash
gcloud run services logs tail trafik-backend --region europe-north1
```

## frontend

> Commands for the Svelte frontend.

### frontend install

> Install frontend dependencies.

```bash
cd frontend
npm i
```

### frontend dev

> Start the Svelte dev server.

```bash
cd frontend
npm run dev
```

### frontend build

> Build the frontend for production.

```bash
cd frontend
npm run build
```

## firmware

> Commands for the ESP32-C3 firmware (requires PlatformIO).

### firmware build

> Compile the firmware without flashing.

```bash
cd firmware
cp -n include/config.example.h include/config.h 2>/dev/null || true
pio run
```

### firmware flash

> Compile and flash to a connected ESP32-C3 via USB.

```bash
cd firmware
pio run --target upload
```

### firmware monitor

> Open the serial monitor (115200 baud).

```bash
cd firmware
pio device monitor
```
