# Use Case: Import Migration Data
## Objective
Import compatible export package into target device.
## Actors
Admin.
## Preconditions
Valid package format and version compatibility.
## Main flow
1. Upload package.
2. Validate schema/version.
3. Apply config + metadata.
4. Attempt template restore where supported.
## Alternative/error flows
Unsupported template import -> keep metadata, report limitation.
## Persistence implications
Writes config and migration metadata.
## MQTT implications
Optional status event after import.
## UI implications
Show partial-import warnings clearly.
## Test strategy
Use sample package fixtures for success/failure cases.
