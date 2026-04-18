# Use Case: Demo Mode
## Objective
Allow pre-init hardware validation only once before real initialization.
## Actors
Local admin/technician.
## Preconditions
`initialized=false` and demo not consumed.
## Main flow
1. Admin enters demo mode.
2. Run demo register/check-in/delete sequence.
3. Log and optional LED blink indications.
## Alternative/error flows
Sensor errors shown in logs/UI.
## Persistence implications
Mark demo consumed to prevent reuse after config.
## MQTT implications
No required backend dependency.
## UI implications
Demo button only pre-init.
## Test strategy
Test gating rule transitions around initialization/reset.
