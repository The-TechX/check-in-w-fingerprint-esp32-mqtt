# MQTT Contract (Device-side v1)

## Topic namespace

Assume configurable `topicPrefix` and `deviceId`:

- Commands: `{topicPrefix}/devices/{deviceId}/commands/...`
- Events: `{topicPrefix}/devices/{deviceId}/events/...`
- Status: `{topicPrefix}/devices/{deviceId}/status/...`

## Commands

All commands are JSON payloads. Minimum suggested envelope:

```json
{
  "correlationId": "uuid-or-web-identifier",
  "requestedBy": "server-user-id",
  "timestamp": "2026-04-18T00:00:00Z"
}
```

### Supported command topics

- `.../commands/register/start`
- `.../commands/checkin/once`
- `.../commands/fingerprint/delete` (requires `fingerprintId`)
- `.../commands/fingerprint/wipe-all`
- `.../commands/fingerprint/list`

Delete payload example:

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
  "timestampMs": 1710000000000,
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
  "timestampMs": 1710000000000
}
```

### Operation result / failure
- Topic: `.../events/operation-result`

```json
{
  "correlationId": "uuid",
  "deviceId": "devkit-01",
  "fingerprintId": 123,
  "status": "success|error",
  "code": "OK|DELETE_FAILED|...",
  "message": "human readable",
  "timestampMs": 1710000000000
}
```

### Progress event (serial-log equivalent)
- Topic: `.../events/progress`

```json
{
  "deviceId": "devkit-01",
  "command": "fingerprint/delete",
  "stage": "start|finish|validate",
  "status": "progress|success|error",
  "correlationId": "uuid",
  "message": "Delete started",
  "timestampMs": 1710000000000
}
```

### Device status / heartbeat
- Topic: `.../status/heartbeat`

```json
{
  "deviceId": "devkit-01",
  "online": true,
  "queueDepth": 3,
  "timestampMs": 1710000000000
}
```

## Delivery and idempotency guidance

- At-least-once publish behavior is expected.
- Server should deduplicate by `eventId` when available.
- `correlationId` links command-response operations.
- Offline queued events preserve original `eventId` and timestamps.
