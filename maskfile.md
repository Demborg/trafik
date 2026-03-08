# Trafik

> Task runner for the trafik monorepo. Run `mask <command> <subcommand>` from the project root.
> Requires: mask, go, node/npm, platformio (pio), docker, gcloud

## install

> Install dependencies for all components.

```bash
mask install backend & mask install frontend & mask install firmware & wait
```

### install backend

> Download Go module dependencies.

```bash
cd backend
go mod download
```

### install frontend

> Install frontend npm dependencies.

```bash
cd frontend
npm i
```

### install firmware

> Install PlatformIO using the official installer script.

```bash
curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py && python3 get-platformio.py && rm get-platformio.py
mkdir -p ~/.local/bin
ln -sf ~/.platformio/penv/bin/pio ~/.local/bin/pio
```

## dev

> Start both backend and frontend for local development.

```bash
mask dev backend & mask dev frontend & wait
```

### dev backend

> Start the backend server locally.

```bash
cd backend
go run ./cmd/server
```

### dev frontend

> Start the Svelte dev server.

```bash
cd frontend
npm run dev
```

## build

> Build all components.

```bash
mask build backend & mask build frontend & mask build firmware & wait
```

### build backend

> Build the backend Docker image locally.

```bash
docker build -t trafik-backend ./backend
```

### build frontend

> Build the frontend for production.

```bash
cd frontend
npm run build
```

### build firmware

> Compile the firmware without flashing.

```bash
cd firmware
cp -n include/config.example.h include/config.h 2>/dev/null || true
pio run
```

## test

> Run all test suites.

```bash
mask test backend
```

### test backend

> Run all backend unit tests.

```bash
cd backend
go test ./...
```

## deploy

> Deploy all components.

```bash
mask deploy backend & mask deploy firmware & wait
```

### deploy backend

> Manually deploy the backend to Cloud Run (normally handled by GHA on push to main).

```bash
gcloud run deploy trafik-backend \
  --source ./backend \
  --region europe-north1 \
  --allow-unauthenticated
```

### deploy firmware

> Compile and flash firmware to a connected ESP32-C3 via USB.

```bash
cd firmware
pio run --target upload
```

## logs

> Tail logs from all components.

```bash
mask logs backend & mask logs firmware & wait
```

### logs backend

> Tail live logs from the Cloud Run backend.

```bash
gcloud run services logs tail trafik-backend --region europe-north1
```

### logs firmware

> Open the firmware serial monitor (115200 baud).

```bash
cd firmware
pio device monitor
```
