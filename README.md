# ESP32 AS608 MQTT Fingerprint Terminal (ESP-IDF)

Production-minded v1 scaffold for an **ESP32 DevKit V1** fingerprint check-in terminal using **AS608** and **MQTT over TLS**.

## What this repo includes

- ESP-IDF project scaffold with layered components (`domain`, `application`, `infrastructure`, `drivers`, `webui`, `platform`)
- Initial app bootstrap flow with runtime mode selection (initial setup, demo mode, configured mode)
- MQTT/event use-case orchestration and offline queue handling interfaces
- Simple server-rendered web UI stub (HTTP server)
- Host-side tests for core domain + application use-case behaviors
- Documentation-first design with MQTT contract, architecture, and use-case flows

## Quick start (VS Code + ESP-IDF extension)

Follow the complete setup guide (hardware + software prerequisites, IDE setup, and troubleshooting):

- [Getting started](docs/getting-started.md)

Quick commands:
- `idf.py set-target esp32`
- `idf.py reconfigure`
- `idf.py build`
- `idf.py -p <PORT> flash`
- `idf.py -p <PORT> monitor`


## MQTT local end-to-end (Docker Compose)

Se agregó una capa MQTT de transporte en el firmware y un entorno local reproducible en `local/docker`:

```bash
cd local/docker
docker compose up --build
```

Esto levanta:

- Mosquitto en `localhost:1883`
- Consola Node + Express en `http://localhost:3000`

Guía paso a paso: [local/docker/README.md](local/docker/README.md).

### Parámetros que debes alinear en el ESP32

Desde la WebUI del dispositivo, asegúrate de configurar:

- `mqtt_host`: IP del host Docker visible desde el ESP32 (ej. `192.168.1.10`)
- `mqtt_port`: `1883`
- `topic_prefix`: `fingerprint`
- `device_id`: `esp32-fingerprint-01`
- `auth_token`: vacío para broker local sin auth

> `localhost` solo funciona dentro del host o contenedor; el ESP32 debe usar la IP LAN real del broker.

## Host-side tests (without hardware)

```bash
cmake -S test/host -B out/host-tests
cmake --build out/host-tests
ctest --test-dir out/host-tests --output-on-failure
```

## Docs index

- [Getting started](docs/getting-started.md)
- [Architecture overview](docs/architecture.md)
- [MQTT contract](docs/mqtt-contract.md)
- [Hardware integration (ESP32 + AS608)](docs/hardware-integration.md)
- [Use cases](docs/use-cases/)

## Current status

This is a **v1 scaffold** with concrete domain/application orchestration and explicit stubs for hardware/network layers.

> AS608 template export/import capability is intentionally isolated and currently returns unsupported in the stub until validated against exact module firmware/datasheet.
