# Changelog

## 1.0.0 - 2026-08-05

Sweet POWER v1.0.0 release. Architecture frozen as the stable production
baseline for the Sweet Power project family -- see
`SweetPower-Project-Docs/RELEASE_NOTES_v1.0.md` for the full cross-project
release summary. Repository renamed from `ESP-DISPLAY-PROTOCOL` to
`ESP-TELEMETRY` on GitHub to match the library's actual name (the files
always said `ESP-TELEMETRY`; only the repository identity was stale).
Adds, on top of everything below (previously accumulated as unreleased
work, now shipped as 1.0.0): `Decoder` sequence tracking keyed by
`(version, messageType, deviceId)` instead of `(version, messageType)`,
required for the Stage 32 two-inverter architecture where a single
Decoder instance sees two devices' frames interleaved on one shared UART
wire; `TelemetryPeerManager` multi-peer/multi-device trust and independent
per-peer sequence/stale state; `kMaxTelemetryFields` raised to 20 and
`kMaxSequenceTrackers` raised to 16.

- Added protocol v2 grouped semantic telemetry frames.
- Added generated stable field IDs from the LuxPower entity catalog.
- Preserved v1 snapshot encoding and decoding for migration compatibility.
- Added per-version/message-type sequence tracking for mixed v1/v2 streams.
- Documented the v2 header, message types, field encoding, scaling, and bandwidth.
- Expanded protocol tests for v2 frames and mixed v1/v2 decoding.

## 0.1.0 - 2026-08-02

Initial public release.

- Added transport-independent snapshot frame format.
- Added fixed-size encoder for Sweet Power telemetry snapshots.
- Added incremental decoder with resynchronization after noise.
- Added CRC16/Modbus validation.
- Added sequence number gap detection.
- Added protocol version checks.
- Added host protocol tests and a minimal C++ test fixture.
