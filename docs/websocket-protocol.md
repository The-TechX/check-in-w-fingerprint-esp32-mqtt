# WebSocket Protocol (Device-side v1)

## Envelope base

```json
{
  "type": "command|event|response|error",
  "requestId": "req-123",
  "deviceId": "esp32-fingerprint-01",
  "timestamp": "2026-04-18T12:00:00.000Z",
  "payload": {},
  "error": { "code": "...", "message": "..." }
}
```

## Servidor -> ESP32 (`type=command`)

- `enroll_fingerprint`
- `identify_fingerprint`
- `ping`
- `healthcheck`
- `delete_fingerprint`
- `list` (alias soportado: `list_fingerprints`)

## ESP32 -> Servidor

- Eventos: `operation_started`, `place_finger`, `remove_finger`, `place_finger_again`, `fingerprint_enrolled`, `fingerprint_match`, `fingerprint_not_found`, `status_report`, `pong`.
- Respuestas: `operation_result`, `fingerprints_list` con `payload.count` y `payload.ids`.
- Errores: `validation_error`, `sensor_error`, `busy`.

## Correlación

`requestId` viaja desde el comando hasta todos los mensajes asociados.

## Heartbeat

Se recomienda `healthcheck` periódico desde servidor y `pong/status_report` como confirmación.
