#include <stdint.h>
#include <string.h>

#include "DisplayProtocol.h"

static int failures = 0;

static void expect(bool condition)
{
    if (!condition)
    {
        ++failures;
    }
}

int main()
{
    using namespace DisplayProtocol;

    const uint8_t crcFixture[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    expect(crc16(crcFixture, sizeof(crcFixture)) == 0x4B37);

    SnapshotPayload payload{};
    payload.pv1PowerW = 2276;
    payload.pv2PowerW = 1739;
    payload.batterySocX10 = 1000;
    payload.pv1EnergyWh = 123456;
    payload.flags = SnapshotFlagFeedInEnabled | SnapshotFlagTcpConnected | SnapshotFlagInputCacheValid;

    uint8_t frameBytes[kMaxFrameSize]{};
    const size_t frameLength = encodeSnapshot(payload, 1, 100, frameBytes, sizeof(frameBytes));
    expect(frameLength == kHeaderSize + kSnapshotPayloadSize + kCrcSize);

    Decoder decoder;
    Frame frame{};
    DecodeResult result = DecodeResult::None;
    for (size_t i = 0; i < frameLength; ++i)
    {
        result = decoder.feed(frameBytes[i], static_cast<uint32_t>(i), frame);
    }
    expect(result == DecodeResult::FrameReady);
    expect(frame.sequence == 1);
    expect(frame.timestampMs == 100);

    SnapshotPayload decoded{};
    expect(decodeSnapshotPayload(frame, decoded));
    expect(decoded.pv1PowerW == payload.pv1PowerW);
    expect(decoded.pv2PowerW == payload.pv2PowerW);
    expect(decoded.batterySocX10 == payload.batterySocX10);
    expect(decoded.pv1EnergyWh == payload.pv1EnergyWh);
    expect(decoded.flags == payload.flags);

    return failures == 0 ? 0 : 1;
}
