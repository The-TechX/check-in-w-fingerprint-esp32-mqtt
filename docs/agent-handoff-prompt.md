# Prompt de Handoff para Agente Codex

Contexto general:
- Proyecto ESP-IDF para terminal de huella con AS608 + MQTT.
- Estado actual: build y flash exitosos en hardware real (ESP32 por COM5).
- Este handoff resume lo corregido y validado para evitar retrabajo.

Objetivo del agente:
- Continuar desde este punto sin romper lo ya estable.
- Respetar los scopes ya resueltos.
- Si propone cambios, validar con build/flash y reportar regresiones.

## Scope 1: Building y Flashing (RESUELTO)

Problemas detectados y corregidos:
1. Resolucion de componente AS608:
- Se alineo nombre de componente en REQUIRES (`as608`) y se habilito discovery del componente anidado con `EXTRA_COMPONENT_DIRS`.
2. Dependencia MQTT en ESP-IDF v6:
- Se agrego dependencia gestionada `espressif/mqtt` por `idf_component.yml`.
3. NVS requerido para WiFi:
- Se inicializa NVS en `app_main` antes de levantar red.

Archivos relevantes:
- `CMakeLists.txt`
- `components/application/CMakeLists.txt`
- `main/CMakeLists.txt`
- `components/infrastructure/idf_component.yml`
- `main/main.c`

Validacion ya hecha:
- Build exitoso (`espIdfCommands.build`).
- Flash exitoso (`espIdfCommands.flash`) en COM5.

No hacer:
- No revertir discovery de `as608` ni dependencia `espressif/mqtt`.
- No quitar init de NVS.

## Scope 2: SoftAP -> STA (RESUELTO)

Objetivo funcional:
- Arranque inicial en SoftAP para onboarding.
- Usuario ingresa SSID/password.
- Dispositivo cambia a STA con esas credenciales.

Problemas detectados y corregidos:
1. Network manager era stub, ahora implementa flujo real con `esp_netif` + `esp_wifi`.
2. Se agregaron helpers para estado de modo e IP (AP/STA).
3. Se ajusto bootstrap para priorizar `INITIAL_SETUP` cuando no hay init.

Archivos relevantes:
- `components/infrastructure/src/network_manager_stub.c`
- `components/infrastructure/include/infrastructure/network_manager.h`
- `components/infrastructure/CMakeLists.txt`
- `components/application/src/app_controller.c`

Validacion ya hecha:
- Log de SoftAP levantado con SSID `FP-Terminal-Setup`.
- DHCP AP activo (`192.168.4.1`).
- Flujo de cambio a STA implementado desde Web UI.

No hacer:
- No volver a stub plano de red.
- No retirar `esp_netif`/`esp_wifi` de REQUIRES.

## Scope 3: Primera interaccion del workflow Web (RESUELTO)

Objetivo funcional UX:
1. Usuario se conecta al SoftAP.
2. Abre `http://192.168.4.1`.
3. Ve formulario de SSID/password (setup).
4. Guarda credenciales y cambia a STA.
5. Luego accede por IP de la red WiFi cliente y ve pagina esperada de MQTT + demo/use-cases.

Problemas detectados y corregidos:
1. `405 Method Not Allowed` en `/setup/wifi` por mismatch de metodo:
- Se habilito `GET /setup/wifi` ademas de `POST /setup/wifi`.
2. Crash en thread HTTP (`InstrFetchProhibited`):
- Hardening del server: stack mayor + buffer HTML en heap + validacion de registro de handlers.
3. UX de pantalla incorrecta en AP:
- Se prioriza formulario de setup cuando modo actual es SoftAP, aun si hay config previa.

Archivos relevantes:
- `components/webui/src/webui_server.c`
- `components/webui/CMakeLists.txt`

Validacion ya hecha:
- Build/flash exitoso tras correcciones de Web UI.
- Portal en AP funcional con formulario de setup visible.
- Flujo AP->STA operativo con credenciales validas.

No hacer:
- No eliminar handler `GET /setup/wifi`.
- No reducir stack HTTP sin medir.
- No volver a mostrar pantalla MQTT/demo como vista principal mientras esta en SoftAP.

## Estado final consolidado

- Compilacion: OK.
- Flasheo: OK.
- SoftAP onboarding: OK.
- Cambio a STA con credenciales: OK.
- Acceso posterior por IP de STA a pagina de admin (MQTT + demo/use-cases): OK cuando credenciales son correctas.

## Checklist obligatorio para cualquier cambio futuro

1. `espIdfCommands.build` debe terminar sin errores.
2. `espIdfCommands.flash` debe terminar sin errores.
3. En monitor serial debe verse:
- SoftAP activo al inicio cuando aplica.
- Post de setup recibido (si se hace onboarding).
- Intento de conexion STA y, si credenciales correctas, IP obtenida (`STA got IP`).
4. Verificar navegacion:
- AP: pantalla de setup WiFi.
- STA: pantalla de admin MQTT/demo.

## Proxima fase sugerida (si el agente continua)

1. Persistencia real NVS para config (hoy repo de config es base/stub en memoria de proceso).
2. Indicador UI de ultimo estado STA (OK/FAIL + razon).
3. Endpoints de health/status para troubleshooting remoto.
