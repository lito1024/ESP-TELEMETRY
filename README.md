# ESP-DISPLAY-PROTOCOL

Transport-independent binary telemetry protocol for ESP devices.

The library defines a small snapshot frame that can carry inverter/display data
from a gateway to a renderer. It intentionally does not know whether bytes move
over UART, MQTT, ESP-NOW, TCP, files, or test fixtures.

## Architecture

```text
data source -> gateway -> protocol frame bytes -> any transport -> receiver
```

The protocol library provides only:

- frame encoder;
- incremental decoder;
- CRC16/Modbus;
- sequence numbers;
- protocol versioning;
- fixed-width integer payload types.

Transport setup, retries, buffering, logging, networking, and rendering belong
outside this library.

## Frame Version

Supported frame version: `1`.

Current snapshot message type: `1`.

## Snapshot Frame

Snapshot frames are exactly 48 bytes:

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
- reserved bytes for future metrics.

No float values are transmitted on the wire.

CRC is CRC16/Modbus with polynomial `0xA001`, initial value `0xFFFF`, and
little-endian transmission.

See [docs/Protocol.md](docs/Protocol.md) for the complete field map.

## Encoder Example

```cpp
#include "DisplayProtocol.h"

DisplayProtocol::SnapshotPayload payload{};
payload.pv1PowerW = 2276;
payload.pv2PowerW = 1739;
payload.batterySocX10 = 1000;
payload.batteryChargePowerW = 0;
payload.batteryDischargePowerW = 0;
payload.pv1EnergyWh = 123456;
payload.flags = DisplayProtocol::SnapshotFlagTcpConnected |
                DisplayProtocol::SnapshotFlagInputCacheValid;

uint8_t frame[DisplayProtocol::kHeaderSize +
              DisplayProtocol::kSnapshotPayloadSize +
              DisplayProtocol::kCrcSize]{};

const size_t length = DisplayProtocol::encodeSnapshot(
    payload, 42, 12345678, frame, sizeof(frame));
```

## Decoder Example

```cpp
#include "DisplayProtocol.h"

DisplayProtocol::Decoder decoder;
DisplayProtocol::Frame frame{};

for (uint8_t byte : incoming_bytes) {
    const auto result = decoder.feed(byte, now_ms, frame);
    if (result == DisplayProtocol::DecodeResult::FrameReady ||
        result == DisplayProtocol::DecodeResult::SequenceGap) {
        DisplayProtocol::SnapshotPayload snapshot{};
        if (DisplayProtocol::decodeSnapshotPayload(frame, snapshot)) {
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
    https://github.com/lito1024/ESP-DISPLAY-PROTOCOL.git#v0.1.0
```

## Arduino Library Dependency

Use the GitHub repository URL in Arduino tooling and pin project builds to a
tag where the tool supports it:

```text
https://github.com/lito1024/ESP-DISPLAY-PROTOCOL.git#v0.1.0
```

## Version Pinning

Pin releases by tag for firmware projects. Update intentionally after reading
the changelog and rebuilding every consumer.

## Transport

Transport is deliberately outside this library. A receiver can read frame bytes
from UART today and from MQTT, ESP-NOW, TCP, or another stream later without
changing the frame format.
