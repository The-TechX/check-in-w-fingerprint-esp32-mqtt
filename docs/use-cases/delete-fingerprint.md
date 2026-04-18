# Use Case: Delete Fingerprint
## Objective
Delete template in AS608 by fingerprintId.
## Actors
Server/admin, ESP32, AS608.
## Preconditions
FingerprintId provided.
## Main flow
1. Receive delete command.
2. Invoke sensor delete.
3. Publish operation result.
## Alternative/error flows
Sensor delete failed -> failure result.
## Persistence implications
No offline buffering required in v1.
## MQTT implications
Operation result event with correlationId.
## UI implications
Manual delete form in admin page.
## Test strategy
Mock sensor delete success/fail and verify result publication.
