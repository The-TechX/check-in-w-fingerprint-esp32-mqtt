# Local WebSocket test environment

## Start

```bash
docker compose up --build
```

Open `http://localhost:8080`.

## Updated UX

- **Main console (`/`)** in dark mode with live status (`connecting`, `open`, `closed`, `error`).
- **Real-time terminal log** with timestamps, direction tags (`OUT`, `IN`, `SYS`, `ERR`) and serialized payloads.
- **Action buttons without redirects** (`enroll_fingerprint`, `identify_fingerprint`, `healthcheck`, `wipe_all_fingerprints`).
- **Delete by fingerprint ID** from inline input with client-side validation.
- **Dedicated fingerprints page (`/fingerprints`)** with refresh que dispara el comando `list` al ESP32 y renderiza la tabla con la respuesta (`count` + `ids`).

## Endpoints

- ESP32 WebSocket endpoint: `ws://<host>:8080/device`
- Browser live console channel: `ws://<host>:8080/console`
- Command API: `POST /api/command`
- Delete command API: `POST /api/command/delete`
- Fingerprints table API: `GET /api/fingerprints`
- Refresh fingerprints from device: `POST /api/fingerprints/refresh` (envía `list`)

## Useful ESP32 settings

- `websocket_host`: IP or hostname reachable by the device.
- `websocket_port`: `8080` for local tests.
- `websocket_path`: `/device`.
- `tls_enabled`: `false` for local WS.
