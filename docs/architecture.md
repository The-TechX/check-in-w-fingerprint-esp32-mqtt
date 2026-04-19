# Arquitectura

## Capas

- `components/domain`: modelos de negocio.
- `components/application`: casos de uso.
- `components/infrastructure`: red, persistencia y transporte WebSocket.
- `components/webui`: UI administrativa SSR en el ESP32.

## Flujo de comando/evento

1. Servidor -> ESP32: mensaje JSON `type=command`.
2. Adaptador WebSocket valida/despacha a caso de uso.
3. Caso de uso usa driver AS608.
4. ESP32 -> Servidor: `event`, `response` o `error` con `requestId`.

## WS y WSS

- Local/test: `ws://host:port/path`.
- Producción: `wss://host:port/path` con `tls_enabled=true`.
