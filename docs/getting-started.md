# Getting Started

1. Configura WiFi desde la UI inicial del ESP32.
2. Configura endpoint WebSocket:
   - host
   - port
   - path
   - TLS on/off
3. Inicia entorno local (`local/docker`) para pruebas end-to-end.

## Comandos recomendados

```bash
idf.py set-target esp32
idf.py build
idf.py flash monitor
```
