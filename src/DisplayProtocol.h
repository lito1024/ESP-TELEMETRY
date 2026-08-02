#ifndef DISPLAY_PROTOCOL_H
#define DISPLAY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace DisplayProtocol
{

static constexpr uint8_t kMagic[4] = {'S', 'P', 'D', 'P'};
static constexpr uint8_t kVersion = 1;
static constexpr uint8_t kMessageTypeSnapshot = 1;
static constexpr size_t kHeaderSize = 14;
static constexpr size_t kSnapshotPayloadSize = 32;
static constexpr size_t kCrcSize = 2;
static constexpr size_t kMaxPayloadSize = 64;
static constexpr size_t kMaxFrameSize = kHeaderSize + kMaxPayloadSize + kCrcSize;
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

uint16_t crc16(const uint8_t *data, size_t length);
size_t encodeSnapshot(const SnapshotPayload &payload, uint16_t sequence, uint32_t timestampMs, uint8_t *out, size_t capacity);
bool decodeSnapshotPayload(const Frame &frame, SnapshotPayload &payload);

class Decoder
{
public:
    Decoder();

    void reset();
    void resetIfTimedOut(uint32_t nowMs, uint32_t timeoutMs);
    DecodeResult feed(uint8_t byte, uint32_t nowMs, Frame &frame);

private:
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
    bool haveLastSequence_;
    uint16_t lastSequence_;
};

} // namespace DisplayProtocol

#endif
