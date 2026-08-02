# Changelog

## 0.1.0 - 2026-08-02

Initial public release.

- Added transport-independent snapshot frame format.
- Added fixed-size encoder for Sweet Power display telemetry snapshots.
- Added incremental decoder with resynchronization after noise.
- Added CRC16/Modbus validation.
- Added sequence number gap detection.
- Added protocol version checks.
- Added host protocol tests and a minimal C++ test fixture.
