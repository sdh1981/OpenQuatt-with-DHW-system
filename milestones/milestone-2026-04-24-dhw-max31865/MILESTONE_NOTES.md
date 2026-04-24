# Milestone: milestone-2026-04-24-dhw-max31865
Date: 2026-04-24

Included scope:
- DHW FSM + manual relay test mode.
- DHW Modbus tank sensor bridge (RS485 #3).
- Dual MAX31865 PT1000 integration for coil in/out.
- Pin remap for ESP32-S3 safety (avoid PSRAM GPIO35/37):
  - MISO -> GPIO38
  - CS1  -> GPIO47
  - SCK  -> GPIO41
  - MOSI -> GPIO39
  - CS2  -> GPIO2
- Relay pins:
  - DHW valve relay -> GPIO40
  - Boiler/boost relay -> GPIO48

This milestone is a file snapshot backup (not a git tag/commit in this environment).
