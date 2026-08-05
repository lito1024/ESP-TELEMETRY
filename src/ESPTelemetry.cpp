#include "ESPTelemetry.h"

namespace ESPTelemetry
{

namespace
{
void writeU16(uint8_t *out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void writeU32(uint8_t *out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFu);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint16_t readU16(const uint8_t *in)
{
    return static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8);
}

uint32_t readU32(const uint8_t *in)
{
    return static_cast<uint32_t>(in[0]) |
           (static_cast<uint32_t>(in[1]) << 8) |
           (static_cast<uint32_t>(in[2]) << 16) |
           (static_cast<uint32_t>(in[3]) << 24);
}

void encodePayload(const SnapshotPayload &payload, uint8_t *out)
{
    writeU32(out + 0, static_cast<uint32_t>(payload.pv1PowerW));
    writeU32(out + 4, static_cast<uint32_t>(payload.pv2PowerW));
    writeU16(out + 8, payload.batterySocX10);
    writeU32(out + 10, static_cast<uint32_t>(payload.batteryChargePowerW));
    writeU32(out + 14, static_cast<uint32_t>(payload.batteryDischargePowerW));
    writeU32(out + 18, payload.pv1EnergyWh);
    writeU16(out + 22, payload.flags);
    for (size_t i = 0; i < 8; ++i)
    {
        out[24 + i] = payload.reserved[i];
    }
}

size_t encodeFrame(uint8_t version,
                   uint8_t messageType,
                   const uint8_t *payload,
                   uint16_t payloadLength,
                   uint16_t sequence,
                   uint32_t timestampMs,
                   uint8_t *out,
                   size_t capacity)
{
    const size_t frameSize = kHeaderSize + payloadLength + kCrcSize;
    if (out == nullptr || payload == nullptr || payloadLength > kMaxPayloadSize || capacity < frameSize)
    {
        return 0;
    }

    out[0] = kMagic[0];
    out[1] = kMagic[1];
    out[2] = kMagic[2];
    out[3] = kMagic[3];
    out[4] = version;
    out[5] = messageType;
    writeU16(out + 6, payloadLength);
    writeU16(out + 8, sequence);
    writeU32(out + 10, timestampMs);
    for (uint16_t i = 0; i < payloadLength; ++i)
    {
        out[kHeaderSize + i] = payload[i];
    }

    const uint16_t crc = crc16(out, kHeaderSize + payloadLength);
    writeU16(out + kHeaderSize + payloadLength, crc);
    return frameSize;
}

uint8_t groupToByte(TelemetryGroup group)
{
    return static_cast<uint8_t>(group);
}

bool byteToGroup(uint8_t value, TelemetryGroup &group)
{
    switch (value)
    {
        case static_cast<uint8_t>(TelemetryGroup::FastTelemetry):
            group = TelemetryGroup::FastTelemetry;
            return true;
        case static_cast<uint8_t>(TelemetryGroup::EnergyTotals):
            group = TelemetryGroup::EnergyTotals;
            return true;
        case static_cast<uint8_t>(TelemetryGroup::Status):
            group = TelemetryGroup::Status;
            return true;
        case static_cast<uint8_t>(TelemetryGroup::Diagnostics):
            group = TelemetryGroup::Diagnostics;
            return true;
    }
    return false;
}
} // namespace

uint16_t crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            if ((crc & 0x0001u) != 0)
            {
                crc = static_cast<uint16_t>((crc >> 1) ^ 0xA001u);
            }
            else
            {
                crc = static_cast<uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

size_t encodeSnapshot(const SnapshotPayload &payload, uint16_t sequence, uint32_t timestampMs, uint8_t *out, size_t capacity)
{
    uint8_t payloadBytes[kSnapshotPayloadSize]{};
    encodePayload(payload, payloadBytes);
    return encodeFrame(kVersion,
                       kMessageTypeSnapshot,
                       payloadBytes,
                       static_cast<uint16_t>(kSnapshotPayloadSize),
                       sequence,
                       timestampMs,
                       out,
                       capacity);
}

bool decodeSnapshotPayload(const Frame &frame, SnapshotPayload &payload)
{
    if (frame.version != kVersion ||
        frame.messageType != kMessageTypeSnapshot ||
        frame.payloadLength != kSnapshotPayloadSize)
    {
        return false;
    }

    payload.pv1PowerW = static_cast<int32_t>(readU32(frame.payload + 0));
    payload.pv2PowerW = static_cast<int32_t>(readU32(frame.payload + 4));
    payload.batterySocX10 = readU16(frame.payload + 8);
    payload.batteryChargePowerW = static_cast<int32_t>(readU32(frame.payload + 10));
    payload.batteryDischargePowerW = static_cast<int32_t>(readU32(frame.payload + 14));
    payload.pv1EnergyWh = readU32(frame.payload + 18);
    payload.flags = readU16(frame.payload + 22);
    for (size_t i = 0; i < 8; ++i)
    {
        payload.reserved[i] = frame.payload[24 + i];
    }
    return true;
}

uint8_t snapshotDeviceId(const SnapshotPayload &payload)
{
    return payload.reserved[0];
}

void setSnapshotDeviceId(SnapshotPayload &payload, uint8_t deviceId)
{
    payload.reserved[0] = deviceId;
}

bool messageTypeIsV2Telemetry(uint8_t messageType)
{
    return messageType == kMessageTypeFastTelemetry ||
           messageType == kMessageTypeEnergyTotals ||
           messageType == kMessageTypeStatus ||
           messageType == kMessageTypeDiagnostics;
}

size_t encodeTelemetry(const TelemetryPayload &payload,
                       uint8_t messageType,
                       uint16_t sequence,
                       uint32_t timestampMs,
                       uint8_t *out,
                       size_t capacity)
{
    if (!messageTypeIsV2Telemetry(messageType) || payload.fieldCount > kMaxTelemetryFields)
    {
        return 0;
    }

    const uint16_t payloadLength =
        static_cast<uint16_t>(kTelemetryPayloadHeaderSize + (payload.fieldCount * kTelemetryFieldRecordSize));
    if (payloadLength > kMaxPayloadSize)
    {
        return 0;
    }

    uint8_t payloadBytes[kMaxPayloadSize]{};
    payloadBytes[0] = payload.deviceId;
    payloadBytes[1] = groupToByte(payload.group);
    payloadBytes[2] = payload.fieldCount;
    writeU16(payloadBytes + 3, payload.flags);

    for (uint8_t i = 0; i < payload.fieldCount; ++i)
    {
        const size_t offset = kTelemetryPayloadHeaderSize + (i * kTelemetryFieldRecordSize);
        writeU16(payloadBytes + offset, payload.fields[i].fieldId);
        writeU32(payloadBytes + offset + 2, static_cast<uint32_t>(payload.fields[i].value));
    }

    return encodeFrame(kVersionV2, messageType, payloadBytes, payloadLength, sequence, timestampMs, out, capacity);
}

bool decodeTelemetryPayload(const Frame &frame, TelemetryPayload &payload)
{
    if (frame.version != kVersionV2 ||
        !messageTypeIsV2Telemetry(frame.messageType) ||
        frame.payloadLength < kTelemetryPayloadHeaderSize)
    {
        return false;
    }

    TelemetryGroup group{};
    if (!byteToGroup(frame.payload[1], group))
    {
        return false;
    }

    const uint8_t fieldCount = frame.payload[2];
    const size_t expectedLength = kTelemetryPayloadHeaderSize + (fieldCount * kTelemetryFieldRecordSize);
    if (fieldCount > kMaxTelemetryFields || frame.payloadLength != expectedLength)
    {
        return false;
    }

    payload.group = group;
    payload.fieldCount = fieldCount;
    payload.deviceId = frame.payload[0];
    payload.flags = readU16(frame.payload + 3);
    for (uint8_t i = 0; i < fieldCount; ++i)
    {
        const size_t offset = kTelemetryPayloadHeaderSize + (i * kTelemetryFieldRecordSize);
        payload.fields[i].fieldId = readU16(frame.payload + offset);
        payload.fields[i].value = static_cast<int32_t>(readU32(frame.payload + offset + 2));
    }
    return true;
}

Decoder::Decoder()
{
    reset();
}

void Decoder::reset()
{
    state_ = State::Magic;
    index_ = 0;
    expectedLength_ = 0;
    magicIndex_ = 0;
    lastByteMs_ = 0;
    for (size_t i = 0; i < kMaxSequenceTrackers; ++i)
    {
        sequenceTrackers_[i] = {false, 0, 0, 0, 0};
    }
}

void Decoder::resetIfTimedOut(uint32_t nowMs, uint32_t timeoutMs)
{
    if (state_ != State::Magic && timeoutMs > 0 && nowMs - lastByteMs_ > timeoutMs)
    {
        state_ = State::Magic;
        index_ = 0;
        expectedLength_ = 0;
        magicIndex_ = 0;
    }
}

DecodeResult Decoder::feed(uint8_t byte, uint32_t nowMs, Frame &frame)
{
    lastByteMs_ = nowMs;

    if (state_ == State::Magic)
    {
        if (magicMatchesPrefix(byte))
        {
            buffer_[magicIndex_ - 1] = byte;
            if (magicIndex_ == sizeof(kMagic))
            {
                state_ = State::Header;
                index_ = sizeof(kMagic);
                magicIndex_ = 0;
            }
        }
        return DecodeResult::None;
    }

    buffer_[index_++] = byte;

    if (state_ == State::Header && index_ == kHeaderSize)
    {
        if (buffer_[4] != kVersion && buffer_[4] != kVersionV2)
        {
            restartWithByte(byte);
            return DecodeResult::UnsupportedVersion;
        }

        const uint16_t payloadLength = readU16(buffer_ + 6);
        if (payloadLength > kMaxPayloadSize)
        {
            restartWithByte(byte);
            return DecodeResult::PayloadTooLarge;
        }

        expectedLength_ = kHeaderSize + payloadLength + kCrcSize;
        state_ = State::PayloadAndCrc;
    }

    if (state_ == State::PayloadAndCrc && expectedLength_ > 0 && index_ == expectedLength_)
    {
        return finishFrame(frame);
    }

    return DecodeResult::None;
}

void Decoder::restartWithByte(uint8_t byte)
{
    state_ = State::Magic;
    index_ = 0;
    expectedLength_ = 0;
    magicIndex_ = 0;
    (void) magicMatchesPrefix(byte);
    if (magicIndex_ > 0)
    {
        buffer_[0] = byte;
    }
}

bool Decoder::magicMatchesPrefix(uint8_t byte)
{
    if (byte == kMagic[magicIndex_])
    {
        ++magicIndex_;
        return true;
    }

    magicIndex_ = (byte == kMagic[0]) ? 1 : 0;
    return magicIndex_ != 0;
}

DecodeResult Decoder::finishFrame(Frame &frame)
{
    const uint16_t expectedCrc = readU16(buffer_ + expectedLength_ - kCrcSize);
    const uint16_t actualCrc = crc16(buffer_, expectedLength_ - kCrcSize);
    if (expectedCrc != actualCrc)
    {
        state_ = State::Magic;
        index_ = 0;
        expectedLength_ = 0;
        magicIndex_ = 0;
        return DecodeResult::BadCrc;
    }

    frame.version = buffer_[4];
    frame.messageType = buffer_[5];
    frame.payloadLength = readU16(buffer_ + 6);
    frame.sequence = readU16(buffer_ + 8);
    frame.timestampMs = readU32(buffer_ + 10);
    for (size_t i = 0; i < frame.payloadLength; ++i)
    {
        frame.payload[i] = buffer_[kHeaderSize + i];
    }

    // Stage 32: identify which device this frame's payload claims to be from
    // so multiple sources sharing this Decoder (e.g. two Gateways forwarded
    // over one Bridge->Display UART wire) get independent sequence trackers
    // instead of having their interleaved sequence numbers compared against
    // each other. Read directly from the already-buffered payload bytes --
    // same offsets encodeTelemetry()/setSnapshotDeviceId() use -- not a full
    // payload decode. Falls back to 0 for anything else (unknown/legacy
    // message types), preserving pre-Stage-32 single-device behavior.
    uint8_t deviceIdForSequencing = 0;
    if (frame.version == kVersionV2 && messageTypeIsV2Telemetry(frame.messageType) && frame.payloadLength >= 1)
    {
        deviceIdForSequencing = frame.payload[0];
    }
    else if (frame.version == kVersion && frame.messageType == kMessageTypeSnapshot &&
             frame.payloadLength >= kSnapshotPayloadSize)
    {
        deviceIdForSequencing = frame.payload[24]; // matches snapshotDeviceId()'s reserved[0]
    }

    DecodeResult result = DecodeResult::FrameReady;
    SequenceTracker *freeTracker = nullptr;
    SequenceTracker *tracker = nullptr;
    for (size_t i = 0; i < kMaxSequenceTrackers; ++i)
    {
        if (sequenceTrackers_[i].used &&
            sequenceTrackers_[i].version == frame.version &&
            sequenceTrackers_[i].messageType == frame.messageType &&
            sequenceTrackers_[i].deviceId == deviceIdForSequencing)
        {
            tracker = &sequenceTrackers_[i];
            break;
        }
        if (!sequenceTrackers_[i].used && freeTracker == nullptr)
        {
            freeTracker = &sequenceTrackers_[i];
        }
    }

    if (tracker == nullptr)
    {
        tracker = freeTracker;
    }

    if (tracker != nullptr)
    {
        if (tracker->used)
        {
            const uint16_t expectedSequence = static_cast<uint16_t>(tracker->lastSequence + 1u);
            if (frame.sequence != expectedSequence)
            {
                result = DecodeResult::SequenceGap;
            }
        }
        tracker->used = true;
        tracker->version = frame.version;
        tracker->messageType = frame.messageType;
        tracker->deviceId = deviceIdForSequencing;
        tracker->lastSequence = frame.sequence;
    }

    state_ = State::Magic;
    index_ = 0;
    expectedLength_ = 0;
    magicIndex_ = 0;
    return result;
}

} // namespace ESPTelemetry
