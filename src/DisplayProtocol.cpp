#include "DisplayProtocol.h"

namespace DisplayProtocol
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
    static constexpr size_t frameSize = kHeaderSize + kSnapshotPayloadSize + kCrcSize;
    if (out == nullptr || capacity < frameSize)
    {
        return 0;
    }

    out[0] = kMagic[0];
    out[1] = kMagic[1];
    out[2] = kMagic[2];
    out[3] = kMagic[3];
    out[4] = kVersion;
    out[5] = kMessageTypeSnapshot;
    writeU16(out + 6, static_cast<uint16_t>(kSnapshotPayloadSize));
    writeU16(out + 8, sequence);
    writeU32(out + 10, timestampMs);
    encodePayload(payload, out + kHeaderSize);

    const uint16_t crc = crc16(out, kHeaderSize + kSnapshotPayloadSize);
    writeU16(out + kHeaderSize + kSnapshotPayloadSize, crc);
    return frameSize;
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
    haveLastSequence_ = false;
    lastSequence_ = 0;
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
        if (buffer_[4] != kVersion)
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

    DecodeResult result = DecodeResult::FrameReady;
    if (haveLastSequence_)
    {
        const uint16_t expectedSequence = static_cast<uint16_t>(lastSequence_ + 1u);
        if (frame.sequence != expectedSequence)
        {
            result = DecodeResult::SequenceGap;
        }
    }

    haveLastSequence_ = true;
    lastSequence_ = frame.sequence;
    state_ = State::Magic;
    index_ = 0;
    expectedLength_ = 0;
    magicIndex_ = 0;
    return result;
}

} // namespace DisplayProtocol
