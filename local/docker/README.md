# Entorno local reproducible (Mosquitto + consola Node)

Esta carpeta levanta todo lo necesario para probar la capa MQTT en local:

- Broker `Mosquitto` (puerto `1883`)
- Servidor `Node.js + Express` con SSR simple y eventos en tiempo real por SSE (puerto `3000`)

## 1) Levantar stack local

```bash
cd local/docker
docker compose up --build
```

URLs y puertos:

- Broker MQTT: `localhost:1883`
- Consola web: `http://localhost:3000`

## 2) Topics usados

Con `TOPIC_PREFIX=fingerprint` y `DEVICE_ID=esp32-fingerprint-01`:

- Commands: `fingerprint/devices/esp32-fingerprint-01/commands/...`
  - `register/start`
  - `checkin/once`
  - `fingerprint/delete`
  - `fingerprint/wipe-all`
  - `fingerprint/list`
- Events:
  - `fingerprint/devices/esp32-fingerprint-01/events/checkin`
  - `fingerprint/devices/esp32-fingerprint-01/events/register-result`
  - `fingerprint/devices/esp32-fingerprint-01/events/operation-result`
  - `fingerprint/devices/esp32-fingerprint-01/events/progress`
- Status:
  - `fingerprint/devices/esp32-fingerprint-01/status/heartbeat`

## 3) Configurar el ESP32

Ajusta la configuración persistida del dispositivo (desde la WebUI del propio ESP32):

- `mqtt_host`: IP de tu máquina Docker en la red del ESP32 (por ejemplo `192.168.1.10`; **no** usar `localhost` en el ESP32)
- `mqtt_port`: `1883`
- `topic_prefix`: `fingerprint` (o el que elijas, pero debe coincidir con Node)
- `device_id`: `esp32-fingerprint-01` (o el que elijas, pero debe coincidir con Node)
- `auth_token`: vacío para esta configuración local (Mosquitto anónimo)

Además, configura:

- `wifi_ssid` y `wifi_password` para que el ESP32 esté en la misma red LAN que el host donde corre Docker.

## 4) Probar ida y vuelta

1. Entra a `http://localhost:3000`.
2. Pulsa comandos (por ejemplo `register/start` o `checkin/once`).
3. Observa eventos en tiempo real en la sección **Live events**.

El ESP32 publicará progreso/éxito/error en `events/progress` y resultados en `events/*` según el caso de uso ejecutado.

## 5) Variables útiles en Docker Compose

Puedes sobreescribir variables del servicio `mqtt-console`:

- `MQTT_URL` (default `mqtt://mosquitto:1883`)
- `TOPIC_PREFIX` (default `fingerprint`)
- `DEVICE_ID` (default `esp32-fingerprint-01`)
- `PORT` (default `3000`)
