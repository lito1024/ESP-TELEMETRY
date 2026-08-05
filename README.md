# ESP-TELEMETRY

Transport-independent binary telemetry protocol and transport abstraction for
ESP devices.

The library defines versioned telemetry frames shared by gateways, displays,
diagnostics tools, simulators, and future transports. It intentionally does not
know the business logic behind a value; producers publish semantic field IDs and
receivers decode them without knowing Modbus, LuxPower, LVGL, or aggregation.

## Architecture

```text
data source -> gateway -> ESP-TELEMETRY frame -> selected transport -> receiver
```

The protocol library provides only:

- v1 compatibility snapshot encoder;
- v2 grouped telemetry encoder;
- incremental decoder;
- CRC16/Modbus;
- sequence numbers;
- protocol versioning;
- fixed-width integer payload types;
- `ITelemetryTransport`;
- UART and ESP-NOW transport adapters.
- fixed-capacity trusted peer management for ESP-NOW sender validation.

Gateway/display applications choose the transport and own retries, buffering,
logging, networking, rendering, and domain-specific behavior.

## Frame Version

Supported frame versions:

- `1`: legacy fixed snapshot frame.
- `2`: grouped semantic telemetry frames.

Current message types:

- `1`: v1 snapshot.
- `16`: fast telemetry.
- `17`: energy totals.
- `18`: status.
- `19`: diagnostics.

## Snapshot Frame

Legacy snapshot frames are exactly 48 bytes:

| Section | Size |
| --- | ---: |
| Header | 14 bytes |
| Snapshot payload | 32 bytes |
| CRC16/Modbus trailer | 2 bytes |

Header fields:

- magic: ASCII `SPDP`;
- version: `1`;
- message type: `1`;
- payload length;
- sequence number;
- sender timestamp in milliseconds.

Payload fields use scaled integers:

- PV1 Power, W;
- PV2 Power, W;
- Battery SOC, percent x 10;
- Battery Charge Power, W;
- Battery Discharge Power, W;
- PV1 Energy Total, Wh;
- flags for feed-in enabled, TCP connected, input cache valid, and holding cache
  valid;
- reserved bytes; byte 0 carries the telemetry `device_id` in Stage 24.2+
  frames.

No float values are transmitted on the wire.

CRC is CRC16/Modbus with polynomial `0xA001`, initial value `0xFFFF`, and
little-endian transmission.

See [docs/Protocol.md](docs/Protocol.md) for the v1 field map and
[docs/ProtocolV2.md](docs/ProtocolV2.md) for v2 grouped telemetry. See
[docs/PeerManager.md](docs/PeerManager.md) for the ESP-NOW trusted peer model.

## Encoder Example

```cpp
#include "ESPTelemetry.h"

ESPTelemetry::SnapshotPayload payload{};
payload.pv1PowerW = 2276;
payload.pv2PowerW = 1739;
payload.batterySocX10 = 1000;
payload.batteryChargePowerW = 0;
payload.batteryDischargePowerW = 0;
payload.pv1EnergyWh = 123456;
payload.flags = ESPTelemetry::SnapshotFlagTcpConnected |
                ESPTelemetry::SnapshotFlagInputCacheValid;

uint8_t frame[ESPTelemetry::kHeaderSize +
              ESPTelemetry::kSnapshotPayloadSize +
              ESPTelemetry::kCrcSize]{};

const size_t length = ESPTelemetry::encodeSnapshot(
    payload, 42, 12345678, frame, sizeof(frame));
```

## Decoder Example

```cpp
#include "ESPTelemetry.h"

ESPTelemetry::Decoder decoder;
ESPTelemetry::Frame frame{};

for (uint8_t byte : incoming_bytes) {
    const auto result = decoder.feed(byte, now_ms, frame);
    if (result == ESPTelemetry::DecodeResult::FrameReady ||
        result == ESPTelemetry::DecodeResult::SequenceGap) {
        ESPTelemetry::SnapshotPayload snapshot{};
        if (ESPTelemetry::decodeSnapshotPayload(frame, snapshot)) {
            // Use snapshot.
        }
    }
}
```

The decoder:

- ignores noise before magic;
- accepts partial frames;
- handles back-to-back frames;
- rejects bad CRC;
- reports unsupported versions;
- reports sequence gaps;
- supports timeout reset for incomplete frames.

## Host Tests

Run the lightweight host protocol tests:

```powershell
python tools\run_protocol_tests.py
```

The tests cover:

- valid frame;
- bad CRC;
- noise before magic;
- partial frame;
- back-to-back frames;
- sequence gap;
- unsupported version.

## PlatformIO Dependency

Pin a release tag:

```ini
lib_deps =
    https://github.com/lito1024/ESP-TELEMETRY.git#v0.2.0
```

## Arduino Library Dependency

Use the GitHub repository URL in Arduino tooling and pin project builds to a
tag where the tool supports it:

```text
https://github.com/lito1024/ESP-TELEMETRY.git#v0.2.0
```

## Version Pinning

Pin releases by tag for firmware projects. Update intentionally after reading
the changelog and rebuilding every consumer.

## Transport

Transport is deliberately outside this library. A receiver can read frame bytes
from UART today and from MQTT, ESP-NOW, TCP, or another stream later without
changing the frame format.
