# Trusted PeerManager

Stage 24.2 adds a fixed-capacity peer trust layer for ESP-NOW transports.

## Device IDs

- `0`: aggregate/system, reserved.
- `1`: inverter 1.
- `2`: inverter 2.
- `3+`: future telemetry sources.

The current LuxPower Gateway hardware test uses `device_id = 1`.

## Trust Model

Each trusted peer maps one station MAC address to one device ID. Duplicate MAC
addresses and duplicate device IDs are configuration conflicts. A frame from an
unknown MAC is rejected before telemetry decoding. A frame from a trusted MAC
whose decoded `device_id` does not match the configured mapping is rejected and
does not update the last valid telemetry.

The manager stores peers in a fixed array with a default capacity of 4 peers.
It does not allocate memory during normal operation.

## Sequence Tracking

Sequence state is tracked per peer/device:

- repeated sequence: duplicate packet;
- skipped sequence: gap;
- `0xFFFF -> 0`: normal uint16 wraparound;
- high sequence followed by `0` or `1`: sender reboot/reset.

Freshness is also per peer. One stale peer does not make another peer stale.
