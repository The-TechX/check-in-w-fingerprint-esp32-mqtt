# MQTT Contract (Device-side v1)

## Topic namespace

Assume configurable `topicPrefix` and `deviceId`:

- Commands: `{topicPrefix}/devices/{deviceId}/commands/...`
- Events: `{topicPrefix}/devices/{deviceId}/events/...`
- Status: `{topicPrefix}/devices/{deviceId}/status/...`

## Commands

### Start registration
- Topic: `.../commands/register/start`
- Payload (JSON):

```json
{
  "correlationId": "uuid",
  "requestedBy": "server-user-id",
  "timestamp": "2026-04-18T00:00:00Z"
}
```

### Delete fingerprint
- Topic: `.../commands/fingerprint/delete`
- Payload:

```json
{
  "correlationId": "uuid",
  "fingerprintId": 123,
  "timestamp": "2026-04-18T00:00:00Z"
}
```

## Events

### Check-in event
- Topic: `.../events/checkin`

```json
{
  "eventId": "checkin-1710000000",
  "deviceId": "devkit-01",
  "fingerprintId": 123,
  "timestamp": "2026-04-18T00:00:00Z",
  "source": "sensor"
}
```

### Registration result
- Topic: `.../events/register-result`

```json
{
  "eventId": "webreg-1710000000",
  "correlationId": "uuid-or-empty",
  "deviceId": "devkit-01",
  "fingerprintId": 123,
  "status": "success",
  "timestamp": "2026-04-18T00:00:00Z"
}
```

### Operation result / failure
- Topic: `.../events/operation-result`

```json
{
  "correlationId": "uuid",
  "deviceId": "devkit-01",
  "operation": "delete-fingerprint",
  "fingerprintId": 123,
  "status": "success",
  "code": "OK",
  "timestamp": "2026-04-18T00:00:00Z"
}
```

### Device status / heartbeat
- Topic: `.../status/heartbeat`

```json
{
  "deviceId": "devkit-01",
  "online": true,
  "queueDepth": 3,
  "timestamp": "2026-04-18T00:00:00Z"
}
```

## Delivery and idempotency guidance

- At-least-once publish behavior is expected.
- Server should deduplicate by `eventId`.
- `correlationId` links command-response operations.
- Offline queued events must preserve original `eventId` and timestamp.
