#include <stdint.h>
#include <string.h>

#include "ESPTelemetry.h"
#include "TelemetryPeerManager.h"

using namespace ESPTelemetry;

static int failures = 0;

static void expect(bool condition)
{
    if (!condition)
    {
        ++failures;
    }
}

static DecodeResult feedFrame(const uint8_t *data, size_t length, Frame &frame)
{
    Decoder decoder;
    DecodeResult result = DecodeResult::None;
    for (size_t i = 0; i < length; ++i)
    {
        result = decoder.feed(data[i], static_cast<uint32_t>(i), frame);
    }
    return result;
}

int main()
{
    const uint8_t crcFixture[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    expect(crc16(crcFixture, sizeof(crcFixture)) == 0x4B37);

    SnapshotPayload payload{};
    payload.pv1PowerW = 2276;
    payload.pv2PowerW = 1739;
    payload.batterySocX10 = 1000;
    payload.pv1EnergyWh = 123456;
    payload.flags = SnapshotFlagFeedInEnabled | SnapshotFlagTcpConnected | SnapshotFlagInputCacheValid;
    setSnapshotDeviceId(payload, kDeviceIdInverter1);

    uint8_t frameBytes[kMaxFrameSize]{};
    const size_t frameLength = encodeSnapshot(payload, 1, 100, frameBytes, sizeof(frameBytes));
    expect(frameLength == kHeaderSize + kSnapshotPayloadSize + kCrcSize);

    Frame frame{};
    expect(feedFrame(frameBytes, frameLength, frame) == DecodeResult::FrameReady);
    expect(frame.sequence == 1);
    expect(frame.timestampMs == 100);

    SnapshotPayload decoded{};
    expect(decodeSnapshotPayload(frame, decoded));
    expect(decoded.pv1PowerW == payload.pv1PowerW);
    expect(decoded.pv2PowerW == payload.pv2PowerW);
    expect(decoded.batterySocX10 == payload.batterySocX10);
    expect(decoded.pv1EnergyWh == payload.pv1EnergyWh);
    expect(decoded.flags == payload.flags);
    expect(snapshotDeviceId(decoded) == kDeviceIdInverter1);

    TelemetryPayload telemetry{};
    telemetry.group = TelemetryGroup::FastTelemetry;
    telemetry.flags = TelemetryGroupFlagValuesValid | TelemetryGroupFlagCacheValid;
    telemetry.fieldCount = 2;
    telemetry.deviceId = kDeviceIdInverter1;
    telemetry.fields[0] = {FieldIdPv1Power, 2276};
    telemetry.fields[1] = {FieldIdBatterySoc, 1000};

    uint8_t telemetryFrame[kMaxFrameSize]{};
    const size_t telemetryLength = encodeTelemetry(telemetry,
                                                   kMessageTypeFastTelemetry,
                                                   2,
                                                   200,
                                                   telemetryFrame,
                                                   sizeof(telemetryFrame));
    expect(telemetryLength == kHeaderSize + kTelemetryPayloadHeaderSize + (2 * kTelemetryFieldRecordSize) + kCrcSize);
    Frame decodedTelemetryFrame{};
    expect(feedFrame(telemetryFrame, telemetryLength, decodedTelemetryFrame) == DecodeResult::FrameReady);
    expect(decodedTelemetryFrame.version == kVersionV2);
    expect(decodedTelemetryFrame.messageType == kMessageTypeFastTelemetry);

    TelemetryPayload decodedTelemetry{};
    expect(decodeTelemetryPayload(decodedTelemetryFrame, decodedTelemetry));
    expect(decodedTelemetry.group == TelemetryGroup::FastTelemetry);
    expect(decodedTelemetry.deviceId == kDeviceIdInverter1);
    expect(decodedTelemetry.fieldCount == 2);
    expect(decodedTelemetry.fields[0].fieldId == FieldIdPv1Power);
    expect(decodedTelemetry.fields[0].value == 2276);
    expect(decodedTelemetry.fields[1].fieldId == FieldIdBatterySoc);
    expect(decodedTelemetry.fields[1].value == 1000);

    TelemetryPayload badTelemetry = telemetry;
    badTelemetry.fieldCount = kMaxTelemetryFields + 1;
    expect(encodeTelemetry(badTelemetry, kMessageTypeFastTelemetry, 3, 300, telemetryFrame, sizeof(telemetryFrame)) == 0);

    uint8_t v1SeqZero[kMaxFrameSize]{};
    const size_t v1SeqZeroLength = encodeSnapshot(payload, 0, 300, v1SeqZero, sizeof(v1SeqZero));
    uint8_t v2SeqZero[kMaxFrameSize]{};
    const size_t v2SeqZeroLength = encodeTelemetry(telemetry,
                                                   kMessageTypeFastTelemetry,
                                                   0,
                                                   400,
                                                   v2SeqZero,
                                                   sizeof(v2SeqZero));
    Decoder mixedDecoder;
    Frame mixedFrame{};
    DecodeResult mixedResult = DecodeResult::None;
    for (size_t i = 0; i < v1SeqZeroLength; ++i)
    {
        mixedResult = mixedDecoder.feed(v1SeqZero[i], static_cast<uint32_t>(i), mixedFrame);
    }
    expect(mixedResult == DecodeResult::FrameReady);
    for (size_t i = 0; i < v2SeqZeroLength; ++i)
    {
        mixedResult = mixedDecoder.feed(v2SeqZero[i], static_cast<uint32_t>(100 + i), mixedFrame);
    }
    expect(mixedResult == DecodeResult::FrameReady);

    TelemetryPeerManager peers;
    const uint8_t mac1[6] = {0x02, 0, 0, 0, 0, 1};
    const uint8_t mac2[6] = {0x02, 0, 0, 0, 0, 2};
    expect(peers.addTrustedPeer(mac1, kDeviceIdInverter1) == PeerRegisterResult::Added);
    expect(peers.addTrustedPeer(mac1, kDeviceIdInverter2) == PeerRegisterResult::DuplicateMac);
    expect(peers.addTrustedPeer(mac2, kDeviceIdInverter1) == PeerRegisterResult::DuplicateDeviceId);
    expect(peers.findByMac(mac1) != nullptr);
    expect(peers.findByDeviceId(kDeviceIdInverter1) != nullptr);

    TelemetryPeerManager sequencePeers;
    expect(sequencePeers.addTrustedPeer(mac1, kDeviceIdInverter1) == PeerRegisterResult::Added);
    expect(sequencePeers.recordReceivedPacket(mac2, kDeviceIdInverter1, 1, 1000, -40) ==
           PeerPacketResult::UnknownSender);
    expect(sequencePeers.recordReceivedPacket(mac1, kDeviceIdInverter2, 1, 1000, -40) ==
           PeerPacketResult::DeviceIdMismatch);
    TelemetryPeerManager cleanPeers;
    expect(cleanPeers.addTrustedPeer(mac1, kDeviceIdInverter1) == PeerRegisterResult::Added);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 10, 1000, -40) ==
           PeerPacketResult::Accepted);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 10, 1100, -40) ==
           PeerPacketResult::DuplicateSequence);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 12, 1200, -40) ==
           PeerPacketResult::SequenceGap);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 0xFFFF, 1300, -40) ==
           PeerPacketResult::SequenceGap);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 0, 1400, -40) ==
           PeerPacketResult::Accepted);
    expect(cleanPeers.recordReceivedPacket(mac1, kDeviceIdInverter1, 0, 1500, -40) ==
           PeerPacketResult::DuplicateSequence);
    const TelemetryPeer *peer = cleanPeers.findByDeviceId(kDeviceIdInverter1);
    expect(peer != nullptr && peer->duplicate_packets == 2);
    expect(peer != nullptr && peer->sequence_gaps == 2);
    expect(!cleanPeers.isStale(*peer, 6000));
    expect(cleanPeers.isStale(*peer, 7001));

    return failures == 0 ? 0 : 1;
}
