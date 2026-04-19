# ESP32 AS608 WebSocket Fingerprint Terminal (ESP-IDF)

Proyecto ESP-IDF para un terminal de huella con sensor AS608 y transporte bidireccional exclusivo por WebSocket.

## Arquitectura actual

- El ESP32 se conecta como cliente a un servidor WebSocket (`ws://` local o `wss://` producción).
- El servidor envía comandos JSON (`type: command`).
- El ESP32 ejecuta casos de uso y responde con eventos/respuestas JSON (`type: event|response|error`).
- Si el canal está caído, los eventos críticos quedan en cola local y se reintentan al reconectar.

## Protocolo

Ver `docs/websocket-protocol.md`.

## Configuración del dispositivo

- `websocket_host`
- `websocket_port`
- `websocket_path`
- `tls_enabled` (`false` para WS local, `true` para WSS)
- `websocket_auth_token` (opcional)
- `device_id`

## Pruebas locales

Se incluye entorno reproducible con Node.js + Express + WebSocket en `local/docker`.

```bash
cd local/docker
docker compose up --build
```

Luego abrir `http://localhost:8080`.

## Documentación

- `docs/architecture.md`
- `docs/getting-started.md`
- `docs/websocket-protocol.md`
- `docs/use-cases/`
