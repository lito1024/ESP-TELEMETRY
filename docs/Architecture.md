# ESP-TELEMETRY Architecture

Stage 23 renames `ESP-DISPLAY-PROTOCOL` to `ESP-TELEMETRY` because the library is
no longer display-specific. It owns the shared telemetry layer used by gateways,
displays, simulators, diagnostics tools, and future transports.

## Repository Responsibilities

`ESP-LUXPOWER` communicates with LuxPower inverters only. It must not know about
UART, ESP-NOW, displays, LVGL, routing, or telemetry transport choices.

`ESP-TELEMETRY` owns packet definitions, field IDs, semantic entity IDs,
`device_id`, CRC, versioning, encoders, decoders, message types, transport
interfaces, and future command protocol shapes. It contains only
transport-independent logic plus reusable transport adapters.

`luxpower-gateway` owns LuxPower polling, cache use, aggregation, health, future
command execution, and transport selection.

`sweet-power-display` receives telemetry, publishes ESPHome entities, and updates
LVGL. It never knows Modbus or LuxPower register mappings.

## Transport Model

Gateway and receiver code should depend on `ESPTelemetry::ITelemetryTransport`
instead of a concrete transport.

Current adapters:

- `UARTTransport`: validation and fallback transport over any Arduino `Stream`.
- `ESPNowTransport`: Stage 24 production wireless adapter. It sends and receives
  complete ESP-TELEMETRY frames, tracks send callbacks, callback failures,
  callback latency, receive drops, packet ages, peer readiness, and RSSI when the
  ESP-IDF receive metadata exposes it.

Future adapters can add UDP, Ethernet, RS485, BLE, or test/simulator transports
without changing packet encoding.

## Production Direction

The intended production architecture is:

```text
LuxPower -> Gateway -> ESP-NOW -> Display
```

UART remains useful for debug, fallback, and validation.

## Stage 24 ESP-NOW Assumptions

- Gateway and Display connect to the same fixed-channel MikroTik 2.4 GHz AP.
- No roaming, channel discovery, or automatic channel switching is implemented.
- Gateway peer MAC is supplied by ignored local transport configuration.
- Display maps trusted Gateway MAC addresses to device IDs before decoding
  ESP-TELEMETRY packets; it does not know LuxPower registers, TCP polling,
  reconnects, or inverter semantics.
- ESP-NOW encryption keys are reserved for future work but are not enabled.

## Device IDs

`device_id` is used for telemetry routing:

- `0`: aggregate/site values
- `1`: inverter 1
- `2`: inverter 2
- `3`: Controller-originated whole-system measurements
- `4`: Garage Sensor local presence source

Future IDs must not change existing field IDs. Display state should be keyed by
`device_id` and field ID so one sender never blocks another.

Stage 24.2 stores `device_id` in v1 snapshot reserved byte 0 and in the v2
telemetry payload envelope.

## Future Commands

Commands are not implemented in Stage 23.

The reserved command direction is:

```text
Display -> device_id -> Gateway -> LuxPower
```

Future command messages must use semantic intents, transaction IDs, ACK,
timeouts, retries, and read-back verification. Raw register writes must not be
exposed to the display.

## Migration Plan

1. Keep `ESP-DISPLAY-PROTOCOL` as the historical repository until the new
   `ESP-TELEMETRY` repository is published.
2. Use local `ESP-TELEMETRY` paths for Stage 23 validation.
3. Keep compatibility headers for `DisplayProtocol.h` and
   `DisplayProtocolFields.h` during the transition.
4. Update consumers to include `ESPTelemetry.h` and `ESPTelemetryFields.h`.
5. Publish a future `ESP-TELEMETRY` release after validation.
6. Update gateway and display dependencies from local paths to the published tag.
