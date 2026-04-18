# Local WebSocket test environment

## Start

```bash
docker compose up --build
```

Open `http://localhost:8080`.

## What it provides

- Express SSR page to send commands manually.
- WebSocket endpoint for the ESP32 at `ws://<host>:8080/device`.
- Event log for messages from/to the device.

## Useful ESP32 settings

- `websocket_host`: IP or hostname reachable by the device.
- `websocket_port`: `8080` for local tests.
- `websocket_path`: `/device`.
- `tls_enabled`: `false` for local WS.
