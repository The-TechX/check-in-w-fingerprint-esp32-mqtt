# Getting Started (ESP32 DevKit V1 + AS608 + ESP-IDF)

Esta guía está pensada para arrancar el proyecto desde cero en un entorno reproducible.

## 1) Requisitos de hardware

### Hardware mínimo
- 1x ESP32 DevKit V1 (WROOM-32)
- 1x Sensor de huella AS608 (módulo UART)
- 1x Cable USB de datos (no solo carga)
- Jumpers Dupont (M-M / M-H según módulo)
- Fuente USB estable (5V) para pruebas

### Hardware recomendado para laboratorio
- Protoboard
- Multímetro
- Convertidor de nivel lógico (si tu AS608 no es 3.3V tolerante en UART)

### Cableado base
| ESP32 DevKit V1 | AS608 | Notas |
|---|---|---|
| GPIO17 (TX2) | RX | UART ESP32 -> AS608 |
| GPIO16 (RX2) | TX | UART AS608 -> ESP32 |
| GND | GND | Tierra común obligatoria |
| 5V o 3V3 (según módulo) | VCC | Verificar hoja de datos del módulo |

> Verificar SIEMPRE nivel lógico UART del AS608 exacto que tengas. Si la salida TX del AS608 supera 3.3V hacia ESP32 RX, usa adaptación de nivel.

## 2) Requisitos de software

### Sistema operativo compatible
- Linux (recomendado para flujo nativo ESP-IDF)
- macOS
- Windows 10/11 (con ESP-IDF extension/toolchain)

### Herramientas obligatorias
- Visual Studio Code
- Extensión **Espressif IDF** para VS Code
- ESP-IDF v5.x (recomendado 5.1+)
- Python 3.10+ (según versión de ESP-IDF)
- Git
- CMake y Ninja (normalmente instalados por ESP-IDF tools)

### Librerías/framework del firmware
Este proyecto usa componentes oficiales de ESP-IDF:
- `esp_http_server`
- `mqtt`
- `nvs_flash`
- `esp_event`
- `esp_timer`
- `driver`

No se usan bibliotecas Arduino en esta base.

## 3) Preparar VS Code + ESP-IDF extension

1. Instalar VS Code y la extensión Espressif IDF.
2. Ejecutar el asistente de configuración de la extensión.
3. Seleccionar/instalar versión ESP-IDF 5.x.
4. Confirmar intérprete Python del entorno ESP-IDF.
5. Abrir este repositorio.

## 4) Configurar proyecto

Desde terminal con entorno ESP-IDF cargado:

```bash
idf.py set-target esp32
idf.py reconfigure
```

## 5) Build, flash y monitor

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

En Windows, el puerto puede ser `COM3`, `COM4`, etc.

## 6) Pruebas de lógica sin hardware

```bash
cmake -S test/host -B out/host-tests
cmake --build out/host-tests
ctest --test-dir out/host-tests --output-on-failure
```

## 7) Validación inicial de hardware

1. Verificar cableado UART + GND común.
2. Encender y abrir monitor serial.
3. Confirmar logs de arranque.
4. Probar handshake AS608 (pendiente de driver real UART en próximos pasos).

## 8) Troubleshooting rápido

- **No detecta puerto serial**: cambiar cable USB, revisar permisos (`dialout` en Linux), verificar driver USB-UART.
- **Build falla por toolchain**: re-ejecutar setup del ESP-IDF extension.
- **Sensor no responde**: revisar TX/RX invertidos, baud rate y alimentación.
- **Reinicios aleatorios**: fuente inestable o consumo pico del sensor.
