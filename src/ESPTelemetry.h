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
static constexpr size_t kHeaderSize = 14;
static constexpr size_t kSnapshotPayloadSize = 32;
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
