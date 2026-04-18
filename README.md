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

1. Install prerequisites:
   - ESP-IDF v5.x toolchain
   - Visual Studio Code
   - ESP-IDF extension for VS Code
2. Open this repository in VS Code.
3. In the ESP-IDF extension:
   - Select target: `esp32`
   - Set flash port for your board
   - Configure project (`idf.py reconfigure`)
4. Build / flash / monitor:
   - `idf.py build`
   - `idf.py -p <PORT> flash`
   - `idf.py -p <PORT> monitor`

See detailed docs in `docs/`.

## Host-side tests (without hardware)

```bash
cmake -S test/host -B out/host-tests
cmake --build out/host-tests
ctest --test-dir out/host-tests --output-on-failure
```

## Docs index

- [Architecture overview](docs/architecture.md)
- [MQTT contract](docs/mqtt-contract.md)
- [Hardware integration (ESP32 + AS608)](docs/hardware-integration.md)
- [Use cases](docs/use-cases/)

## Current status

This is a **v1 scaffold** with concrete domain/application orchestration and explicit stubs for hardware/network layers.

> AS608 template export/import capability is intentionally isolated and currently returns unsupported in the stub until validated against exact module firmware/datasheet.
