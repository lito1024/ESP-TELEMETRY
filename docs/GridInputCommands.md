# Grid-input commands (Stage 53, ESP-028)

Controller (SPC) -> Distribution Node (Waveshare ESP32-S3-Relay-6CH, device 7)
planned isolation of the external-grid input through Relay 3 (ON = grid
isolated). Owner decision D-019 (HA-059) chose variant R: Home Assistant
drives the node's own "Relay 3" switch and the node keeps the final guards
itself (time cap, rate limits, HA-silence timeout). These commands are the
SPC path, kept for later; the node accepts them, no Controller sends them
yet. Manual (HA) and planned (Controller) isolations share the node's limits.

Hand-maintained (not generated): `CommandId`, `GridInputReason`,
`GridInputStatus` and the pack/unpack helpers live in `src/ESPTelemetry.h`.
Wire format is the existing Stage 36 command framing: message type 32
(COMMAND, 8-byte payload) and 33 (COMMAND_RESULT, 17-byte payload). No new
FieldId and no new message type.

## Commands

| CommandId | value | Meaning |
| --- | --- | --- |
| 3 `GridInputIsolate` | requested maximum isolation, s (60..3600) | start a planned isolation, or refresh the running one |
| 4 `GridInputConnect` | 0 | end a planned isolation; never ends a manual one |

**Level-triggered.** The Controller repeats its current wish every few
seconds: `GridInputIsolate` while it wants the grid isolated,
`GridInputConnect` otherwise. The node ends a planned isolation by itself when
the `GridInputIsolate` repeats stop for its link timeout (reason
`ControllerLost`). A repeat never extends the deadline: the deadline is set
once, at the start, as min(requested maximum, node hard cap). A smaller
requested maximum in a repeat may shorten it.

## Result

Every command gets exactly one COMMAND_RESULT:

| Field | Content |
| --- | --- |
| `resultCode` | `Success` (started / ended), `SuccessAlreadySet` (refresh, or already connected), `ValidationError` (value out of range), `Busy` (refused by a node guard) |
| `requestedValue` | the command's value (echo) |
| `previousValue` | packed status word, `packGridInputStatusWord()` |
| `readbackValue` | seconds left of the planned isolation, 0 if none |

Status word bits:

| Bits | Content |
| --- | --- |
| 0-7 | `GridInputReason` |
| 8 | Relay 3 ON (grid isolated: planned or manual) |
| 9 | planned isolation active |
| 10 | manual isolation active (HA / web switch) |
| 16-23 | planned isolations started in the last hour |
| 24-30 | planned isolations started in the last 24 h (saturates at 127) |

`GridInputReason`: 0 None, 1 NodeRestart, 2 Request, 3 Released, 4 TimeLimit,
5 ControllerLost, 6 RateLimit, 7 MinGap, 8 ManualActive, 9 InvalidValue,
10 Manual, 11 HaLost (manual isolation ended: HA API silent). A refusal
leaves the state unchanged and reports its own reason.

## Compatibility

A Controller that never sends these commands never receives a result, so the
node's existing V0 telemetry (FieldId 14001, FAST_TELEMETRY) is unchanged and
an older Controller keeps working as before.
