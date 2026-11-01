#ifndef ESP_TELEMETRY_H
#define ESP_TELEMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "ESPTelemetryFields.h"

namespace ESPTelemetry
{

static constexpr uint8_t kMagic[4] = {'S', 'P', 'D', 'P'};
static constexpr uint8_t kVersion = 1;
static constexpr uint8_t kVersionV2 = 2;
static constexpr uint8_t kMessageTypeSnapshot = 1;
static constexpr uint8_t kMessageTypeFastTelemetry = 16;
static constexpr uint8_t kMessageTypeEnergyTotals = 17;
static constexpr uint8_t kMessageTypeStatus = 18;
static constexpr uint8_t kMessageTypeDiagnostics = 19;
// Stage 36: Controller -> Gateway typed commands and Gateway -> Controller
// command results. v2 messages, same framing as telemetry. deviceId at
// payload[0] in both, matching the existing v2 telemetry convention, so
// Bridge can route/sequence-track by peeking that one byte without decoding
// command semantics (see stages/Stage36.md).
static constexpr uint8_t kMessageTypeCommand = 32;
static constexpr uint8_t kMessageTypeCommandResult = 33;
static constexpr size_t kHeaderSize = 14;
static constexpr size_t kSnapshotPayloadSize = 32;
static constexpr size_t kCommandPayloadSize = 8;
static constexpr size_t kCommandResultPayloadSize = 17;
static constexpr size_t kCrcSize = 2;
static constexpr size_t kMaxPayloadSize = 128;
static constexpr size_t kMaxFrameSize = kHeaderSize + kMaxPayloadSize + kCrcSize;
static constexpr size_t kTelemetryPayloadHeaderSize = 5;
static constexpr size_t kTelemetryFieldRecordSize = 6;
// Capacity note (Stage 30): kMaxPayloadSize (128) already limits a v2
// telemetry payload to floor((128-5)/6) = 20 field records; 20 simply lets
// a single frame use the full payload capacity that was already reserved
// -- no frame/header/CRC format change.
static constexpr size_t kMaxTelemetryFields = 20;
static constexpr uint32_t kInvalidI32 = 0x80000000UL;
static constexpr uint16_t kInvalidU16 = 0xFFFF;
static constexpr uint32_t kInvalidU32 = 0xFFFFFFFFUL;

enum class DecodeResult : uint8_t
{
    None,
    FrameReady,
    SequenceGap,
    BadCrc,
    UnsupportedVersion,
    PayloadTooLarge
};

// Stage 36: the complete, permanent command allowlist. No generic
// writeRegister(address, value) exists anywhere in this protocol or in any
// component that uses it -- adding a new writable capability means adding a
// new named CommandId here, not parameterizing an existing one. See
// stages/Stage36.md "Gateway Command Allowlist".
enum class CommandId : uint8_t
{
    SetMaxBackflowPower = 1,
    SetZeroExport = 2,
    // Stage 53 (ESP-028, D-019): Controller -> Distribution Node (device 7)
    // planned grid-input isolation through Waveshare Relay 3. Level-triggered:
    // the Controller repeats its current wish every few seconds, and the node
    // ends a planned isolation by itself when the repeats stop. See
    // docs/GridInputCommands.md.
    //   GridInputIsolate: value = requested maximum isolation, seconds
    //                     (kGridInputMinIsolationSeconds..kGridInputMaxIsolationSeconds);
    //                     starts a planned isolation or refreshes the running one.
    //   GridInputConnect: value = 0; ends a planned isolation (never a manual one).
    GridInputIsolate = 3,
    GridInputConnect = 4
};

// Terminal result codes for a completed command. Every command reaches
// exactly one of these -- there is no "in progress" value on the wire;
// in-flight state is tracked locally by the sender (Controller), not
// encoded in a CommandResultPayload.
enum class CommandResultCode : uint8_t
{
    Success = 0,
    SuccessAlreadySet = 1,
    Timeout = 2,
    CommunicationError = 3,
    ValidationError = 4,
    ReadbackFailed = 5,
    UnknownDevice = 6,
    UnsupportedCommand = 7,
    // Stage 36: returned locally by Controller (never sent over the wire)
    // when a new command targets a device_id that already has a command
    // in flight -- see EspTelemetryEspNowComponent's per-device pending
    // tracking. Included here so Gateway and Controller share one
    // authoritative result-code enum instead of each defining their own.
    Busy = 8
};

struct CommandPayload
{
    uint8_t deviceId;
    uint8_t commandId;      // CommandId
    uint16_t commandSequence;
    int32_t value;          // percent (0-100) for SetMaxBackflowPower, 0/1 for SetZeroExport
};

struct CommandResultPayload
{
    uint8_t deviceId;
    uint8_t commandId;      // CommandId this result answers
    uint16_t commandSequence; // echoes the CommandPayload that triggered this result
    uint8_t resultCode;     // CommandResultCode
    int32_t requestedValue;
    int32_t previousValue;
    int32_t readbackValue;
};

// Stage 53 (ESP-028): why the grid input is in its current state, as kept by
// the Distribution Node. Carried in every grid-input CommandResult (packed,
// see GridInputStatus). A refusal leaves the state unchanged and reports its
// own reason (RateLimit / MinGap / ManualActive / InvalidValue).
enum class GridInputReason : uint8_t
{
    None = 0,
    NodeRestart = 1,   // node booted; Relay 3 forced OFF (grid connected)
    Request = 2,       // planned isolation started by GridInputIsolate
    Released = 3,      // planned isolation ended by GridInputConnect
    TimeLimit = 4,     // isolation reached its deadline (node hard cap or requested maximum)
    ControllerLost = 5, // no GridInputIsolate repeat within the node's link timeout
    RateLimit = 6,     // refused: per-hour or per-day isolation limit reached
    MinGap = 7,        // refused: too soon after the previous isolation
    ManualActive = 8,  // refused: Relay 3 is held ON manually (HA / web)
    InvalidValue = 9,  // refused: requested maximum outside the allowed range
    Manual = 10,       // Relay 3 switched manually (HA / web)
    HaLost = 11        // manual isolation ended: HA API silent for the node's HA timeout
};

static constexpr int32_t kGridInputMinIsolationSeconds = 60;
static constexpr int32_t kGridInputMaxIsolationSeconds = 3600;

// Stage 53 (ESP-028): Distribution Node grid-input state, returned in the
// CommandResult of every GridInputIsolate / GridInputConnect:
//   requestedValue = the command's value (echo)
//   previousValue  = packGridInputStatusWord(): bits 0-7 reason,
//                    bit 8 Relay 3 ON (grid isolated), bit 9 planned isolation,
//                    bit 10 manual isolation, bits 16-23 planned isolations
//                    started in the last hour, bits 24-30 in the last 24 h
//   readbackValue  = seconds left of the planned isolation (0 if none)
struct GridInputStatus
{
    bool relayIsolated;
    bool plannedIsolation;
    bool manualIsolation;
    GridInputReason reason;
    uint8_t isolationsLastHour;
    uint8_t isolationsLast24h; // saturates at 127
    int32_t remainingSeconds;
};

inline int32_t packGridInputStatusWord(const GridInputStatus &status)
{
    const uint32_t last24h = status.isolationsLast24h > 127 ? 127u : status.isolationsLast24h;
    const uint32_t word = static_cast<uint32_t>(static_cast<uint8_t>(status.reason)) |
                          (status.relayIsolated ? (1u << 8) : 0u) | (status.plannedIsolation ? (1u << 9) : 0u) |
                          (status.manualIsolation ? (1u << 10) : 0u) |
                          (static_cast<uint32_t>(status.isolationsLastHour) << 16) | (last24h << 24);
    return static_cast<int32_t>(word);
}

inline GridInputStatus unpackGridInputStatus(int32_t statusWord, int32_t remainingSeconds)
{
    const uint32_t word = static_cast<uint32_t>(statusWord);
    GridInputStatus status{};
    status.reason = static_cast<GridInputReason>(word & 0xFFu);
    status.relayIsolated = (word & (1u << 8)) != 0;
    status.plannedIsolation = (word & (1u << 9)) != 0;
    status.manualIsolation = (word & (1u << 10)) != 0;
    status.isolationsLastHour = static_cast<uint8_t>((word >> 16) & 0xFFu);
    status.isolationsLast24h = static_cast<uint8_t>((word >> 24) & 0x7Fu);
    status.remainingSeconds = remainingSeconds < 0 ? 0 : remainingSeconds;
    return status;
}

enum SnapshotFlags : uint16_t
{
    SnapshotFlagFeedInEnabled = 1u << 0,
    SnapshotFlagTcpConnected = 1u << 1,
    SnapshotFlagInputCacheValid = 1u << 2,
    SnapshotFlagHoldingCacheValid = 1u << 3
};

struct SnapshotPayload
{
    int32_t pv1PowerW;
    int32_t pv2PowerW;
    uint16_t batterySocX10;
    int32_t batteryChargePowerW;
    int32_t batteryDischargePowerW;
    uint32_t pv1EnergyWh;
    uint16_t flags;
    uint8_t reserved[8];
};

struct Frame
{
    uint8_t version;
    uint8_t messageType;
    uint16_t payloadLength;
    uint16_t sequence;
    uint32_t timestampMs;
    uint8_t payload[kMaxPayloadSize];
};

enum TelemetryGroupFlags : uint16_t
{
    TelemetryGroupFlagValuesValid = 1u << 0,
    TelemetryGroupFlagCacheValid = 1u << 1
};

struct TelemetryFieldValue
{
    uint16_t fieldId;
    int32_t value;
};

struct TelemetryPayload
{
    TelemetryGroup group;
    uint16_t flags;
    uint8_t fieldCount;
    uint8_t deviceId;
    TelemetryFieldValue fields[kMaxTelemetryFields];
};

uint16_t crc16(const uint8_t *data, size_t length);
size_t encodeSnapshot(const SnapshotPayload &payload, uint16_t sequence, uint32_t timestampMs, uint8_t *out, size_t capacity);
bool decodeSnapshotPayload(const Frame &frame, SnapshotPayload &payload);
uint8_t snapshotDeviceId(const SnapshotPayload &payload);
void setSnapshotDeviceId(SnapshotPayload &payload, uint8_t deviceId);
size_t encodeTelemetry(const TelemetryPayload &payload,
                       uint8_t messageType,
                       uint16_t sequence,
                       uint32_t timestampMs,
                       uint8_t *out,
                       size_t capacity);
bool decodeTelemetryPayload(const Frame &frame, TelemetryPayload &payload);
bool messageTypeIsV2Telemetry(uint8_t messageType);
size_t encodeCommand(const CommandPayload &payload, uint16_t sequence, uint32_t timestampMs, uint8_t *out, size_t capacity);
bool decodeCommandPayload(const Frame &frame, CommandPayload &payload);
size_t encodeCommandResult(const CommandResultPayload &payload, uint16_t sequence, uint32_t timestampMs, uint8_t *out, size_t capacity);
bool decodeCommandResultPayload(const Frame &frame, CommandResultPayload &payload);

class Decoder
{
public:
    Decoder();

    void reset();
    void resetIfTimedOut(uint32_t nowMs, uint32_t timeoutMs);
    DecodeResult feed(uint8_t byte, uint32_t nowMs, Frame &frame);

private:
    struct SequenceTracker
    {
        bool used;
        uint8_t version;
        uint8_t messageType;
        uint8_t deviceId;
        uint16_t lastSequence;
    };

    // Stage 32: keyed by (version, messageType, deviceId) rather than just
    // (version, messageType) so a single Decoder instance fed an interleaved
    // byte stream from multiple telemetry sources (e.g. the Display's UART
    // receiver, once the Bridge forwards two Gateways over one wire) does
    // not conflate independent senders' sequence counters into spurious
    // gaps. deviceId is read directly from the already-buffered frame
    // payload (byte 0 for v2 telemetry, byte 24 for v1 snapshot) -- no wire
    // format change, no extra decode pass. Sized for 2 devices x 5 streams
    // (v1 snapshot + v2 FAST_TELEMETRY/ENERGY_TOTALS/STATUS/DIAGNOSTICS)
    // with headroom.
    static constexpr size_t kMaxSequenceTrackers = 16;

    enum class State : uint8_t
    {
        Magic,
        Header,
        PayloadAndCrc
    };

    void restartWithByte(uint8_t byte);
    bool magicMatchesPrefix(uint8_t byte);
    DecodeResult finishFrame(Frame &frame);

    State state_;
    uint8_t buffer_[kMaxFrameSize];
    size_t index_;
    size_t expectedLength_;
    size_t magicIndex_;
    uint32_t lastByteMs_;
    SequenceTracker sequenceTrackers_[kMaxSequenceTrackers];
};

} // namespace ESPTelemetry

#endif
