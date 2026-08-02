# ESP-DISPLAY-PROTOCOL Wire Format

Version: 1

## Byte Order

All integer fields are little-endian.

## CRC

CRC16/Modbus:

- polynomial: `0xA001`;
- initial value: `0xFFFF`;
- CRC covers header and payload;
- CRC field itself is excluded;
- CRC is transmitted little-endian.

Known fixture: CRC16/Modbus of ASCII `123456789` is `0x4B37`.

## Frame Layout

| Offset | Size | Field | Description |
| ---: | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `SPDP` |
| 4 | 1 | Version | `1` |
| 5 | 1 | Message type | `1` = snapshot |
| 6 | 2 | Payload length | Snapshot payload is 32 bytes |
| 8 | 2 | Sequence | Increments by 1 per transmitted frame |
| 10 | 4 | Timestamp | Sender timestamp in milliseconds |
| 14 | N | Payload | Message-specific payload |
| 14 + N | 2 | CRC16 | CRC over header + payload |

Current snapshot frame size: 48 bytes.

## Snapshot Payload

Payload size: 32 bytes.

| Offset | Size | Type | Field | Scale | Invalid |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 4 | `int32` | PV1 power | W x 1 | `INT32_MIN` |
| 4 | 4 | `int32` | PV2 power | W x 1 | `INT32_MIN` |
| 8 | 2 | `uint16` | Battery SOC | percent x 10 | `0xFFFF` |
| 10 | 4 | `int32` | Battery charge power | W x 1 | `INT32_MIN` |
| 14 | 4 | `int32` | Battery discharge power | W x 1 | `INT32_MIN` |
| 18 | 4 | `uint32` | PV1 energy total | Wh x 1 | `0xFFFFFFFF` |
| 22 | 2 | `uint16` | Flags | bitmask | - |
| 24 | 8 | bytes | Reserved | future metrics | zero |

## Flags

| Bit | Name | Meaning |
| ---: | --- | --- |
| 0 | Feed-In Enabled | Verified feed-in-grid setting is enabled |
| 1 | TCP Connected | Gateway is currently connected to the data source |
| 2 | Input Cache Valid | Input cache is valid |
| 3 | Holding Cache Valid | Holding cache is valid |
| 4-15 | Reserved | Must be transmitted as 0 in version 1 |

## Decoder Behavior

The incremental decoder:

- ignores noise before magic;
- accepts partial frames byte by byte;
- decodes back-to-back frames;
- rejects bad CRC;
- reports unsupported protocol version;
- reports payloads larger than the fixed decoder capacity;
- reports sequence gaps;
- supports timeout reset for incomplete frames.

## Versioning

Current protocol version: `1`.

Future incompatible frame changes must increment the version. Future compatible
snapshot metrics should first use reserved bytes or a new message type.
