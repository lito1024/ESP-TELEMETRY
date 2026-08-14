#ifndef ESP_TELEMETRY_PEER_MANAGER_H
#define ESP_TELEMETRY_PEER_MANAGER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ESPTelemetry
{

static constexpr uint8_t kDeviceIdAggregate = 0;
static constexpr uint8_t kDeviceIdInverter1 = 1;
static constexpr uint8_t kDeviceIdInverter2 = 2;
static constexpr uint8_t kDeviceIdControllerSystem = 3;
static constexpr uint8_t kDeviceIdGarageSensor = 4;
static constexpr size_t kDefaultTelemetryPeerCapacity = 4;

// One logical telemetry stream is identified by (protocol version, message type).
// Today that is: v1 snapshot (1 stream) plus v2 FAST_TELEMETRY, ENERGY_TOTALS,
// STATUS, DIAGNOSTICS (4 streams) = 5. Kept generic (no dependency on
// ESPTelemetry.h message-type constants) to match this header's existing
// decoupled design, mirroring ESPTelemetry::Decoder::SequenceTracker.
static constexpr size_t kMaxTelemetryStreamsPerPeer = 5;

struct TelemetryStreamState
{
    bool used;
    uint8_t version;
    uint8_t messageType;
    uint16_t last_sequence;
    uint32_t last_packet_ms;
    uint32_t duplicate_packets;
    uint32_t sequence_gaps;
    uint32_t reboot_resets;
};

struct TelemetryPeer
{
    uint8_t mac[6];
    uint8_t device_id;
    bool trusted;
    bool online;
    bool config_conflict;
    int8_t rssi;
    uint16_t last_sequence;
    uint32_t last_packet_ms;
    uint32_t accepted_packets;
    uint32_t rejected_packets;
    uint32_t duplicate_packets;
    uint32_t sequence_gaps;
    uint32_t reboot_resets;
    TelemetryStreamState streams[kMaxTelemetryStreamsPerPeer];
};

enum class PeerRegisterResult : uint8_t
{
    Added,
    DuplicateMac,
    DuplicateDeviceId,
    InvalidDeviceId,
    CapacityFull
};

enum class PeerPacketResult : uint8_t
{
    Accepted,
    UnknownSender,
    DeviceIdMismatch,
    DuplicateSequence,
    SequenceGap,
    SenderReboot,
    ConfigConflict,
    InvalidDeviceId
};

class TelemetryPeerManager
{
public:
    explicit TelemetryPeerManager(uint32_t staleTimeoutMs = 5000)
        : staleTimeoutMs_(staleTimeoutMs)
    {
    }

    PeerRegisterResult addTrustedPeer(const uint8_t *mac, uint8_t deviceId)
    {
        if (mac == nullptr || deviceId == kDeviceIdAggregate)
        {
            configConflict_ = true;
            return PeerRegisterResult::InvalidDeviceId;
        }

        TelemetryPeer *byMac = findByMac(mac);
        TelemetryPeer *byDevice = findByDeviceId(deviceId);
        if (byMac != nullptr)
        {
            if (byMac->device_id != deviceId)
            {
                byMac->config_conflict = true;
                configConflict_ = true;
            }
            return PeerRegisterResult::DuplicateMac;
        }
        if (byDevice != nullptr)
        {
            byDevice->config_conflict = true;
            configConflict_ = true;
            return PeerRegisterResult::DuplicateDeviceId;
        }
        if (count_ >= kDefaultTelemetryPeerCapacity)
        {
            configConflict_ = true;
            return PeerRegisterResult::CapacityFull;
        }

        TelemetryPeer &peer = peers_[count_++];
        memset(&peer, 0, sizeof(peer));
        memcpy(peer.mac, mac, sizeof(peer.mac));
        peer.device_id = deviceId;
        peer.trusted = true;
        peer.rssi = 0;
        return PeerRegisterResult::Added;
    }

    TelemetryPeer *findByMac(const uint8_t *mac)
    {
        if (mac == nullptr)
        {
            return nullptr;
        }
        for (size_t i = 0; i < count_; ++i)
        {
            if (memcmp(peers_[i].mac, mac, sizeof(peers_[i].mac)) == 0)
            {
                return &peers_[i];
            }
        }
        return nullptr;
    }

    const TelemetryPeer *findByMac(const uint8_t *mac) const
    {
        return const_cast<TelemetryPeerManager *>(this)->findByMac(mac);
    }

    TelemetryPeer *findByDeviceId(uint8_t deviceId)
    {
        for (size_t i = 0; i < count_; ++i)
        {
            if (peers_[i].device_id == deviceId)
            {
                return &peers_[i];
            }
        }
        return nullptr;
    }

    const TelemetryPeer *findByDeviceId(uint8_t deviceId) const
    {
        return const_cast<TelemetryPeerManager *>(this)->findByDeviceId(deviceId);
    }

    // Index-based accessor for iterating registered peers (e.g. for a compact
    // diagnostics log). Returns nullptr for index >= count().
    const TelemetryPeer *peerAt(size_t index) const
    {
        if (index >= count_)
        {
            return nullptr;
        }
        return &peers_[index];
    }

    // previousSequenceOut receives the stream's last-known sequence before this
    // packet, for diagnostic logging of gaps/duplicates. Only meaningful when
    // the stream had already seen a packet; left at 0 otherwise (first packet
    // for that stream is always Accepted, so callers only read this on
    // DuplicateSequence/SequenceGap/SenderReboot results).
    PeerPacketResult recordReceivedPacket(const uint8_t *mac,
                                          uint8_t deviceId,
                                          uint8_t version,
                                          uint8_t messageType,
                                          uint16_t sequence,
                                          uint32_t nowMs,
                                          int8_t rssi,
                                          uint16_t &previousSequenceOut)
    {
        previousSequenceOut = 0;
        if (deviceId == kDeviceIdAggregate)
        {
            ++identityMismatchRejects_;
            return PeerPacketResult::InvalidDeviceId;
        }

        TelemetryPeer *peer = findByMac(mac);
        if (peer == nullptr || !peer->trusted)
        {
            ++unknownSenderRejects_;
            return PeerPacketResult::UnknownSender;
        }
        if (peer->config_conflict)
        {
            ++peer->rejected_packets;
            return PeerPacketResult::ConfigConflict;
        }
        if (peer->device_id != deviceId)
        {
            ++identityMismatchRejects_;
            ++peer->rejected_packets;
            peer->config_conflict = true;
            return PeerPacketResult::DeviceIdMismatch;
        }

        // Sequence continuity is tracked per logical stream (version, messageType),
        // not per peer: the sender maintains independent counters per stream (e.g.
        // v1 snapshot vs. v2 FAST_TELEMETRY vs. v2 STATUS), so interleaved streams
        // must never be compared against each other's sequence numbers.
        TelemetryStreamState *stream = findOrAllocateStream(*peer, version, messageType);

        PeerPacketResult result = PeerPacketResult::Accepted;
        if (stream != nullptr && stream->used)
        {
            previousSequenceOut = stream->last_sequence;
            const uint16_t expected = static_cast<uint16_t>(stream->last_sequence + 1u);
            if (sequence == stream->last_sequence)
            {
                ++stream->duplicate_packets;
                ++peer->duplicate_packets;
                ++peer->rejected_packets;
                return PeerPacketResult::DuplicateSequence;
            }
            else if (sequence != expected)
            {
                if (sequence <= 1 && stream->last_sequence > 100)
                {
                    ++stream->reboot_resets;
                    ++peer->reboot_resets;
                    result = PeerPacketResult::SenderReboot;
                }
                else
                {
                    ++stream->sequence_gaps;
                    ++peer->sequence_gaps;
                    result = PeerPacketResult::SequenceGap;
                }
            }
        }

        if (stream != nullptr)
        {
            stream->used = true;
            stream->version = version;
            stream->messageType = messageType;
            stream->last_sequence = sequence;
            stream->last_packet_ms = nowMs;
        }

        ++peer->accepted_packets;
        peer->online = true;
        peer->rssi = rssi;
        peer->last_sequence = sequence;
        peer->last_packet_ms = nowMs;
        return result;
    }

    void recordMalformedForMac(const uint8_t *mac)
    {
        TelemetryPeer *peer = findByMac(mac);
        if (peer != nullptr)
        {
            ++peer->rejected_packets;
        }
    }

    void recordUnknownSender()
    {
        ++unknownSenderRejects_;
    }

    bool isStale(const TelemetryPeer &peer, uint32_t nowMs) const
    {
        return peer.accepted_packets == 0 || nowMs - peer.last_packet_ms > staleTimeoutMs_;
    }

    void updateStaleStates(uint32_t nowMs)
    {
        for (size_t i = 0; i < count_; ++i)
        {
            peers_[i].online = !isStale(peers_[i], nowMs);
        }
    }

    void setStaleTimeout(uint32_t staleTimeoutMs) { staleTimeoutMs_ = staleTimeoutMs; }
    uint32_t staleTimeout() const { return staleTimeoutMs_; }
    size_t count() const { return count_; }
    size_t capacity() const { return kDefaultTelemetryPeerCapacity; }
    bool configConflict() const { return configConflict_; }
    uint32_t unknownSenderRejects() const { return unknownSenderRejects_; }
    uint32_t identityMismatchRejects() const { return identityMismatchRejects_; }

private:
    // Finds the stream slot matching (version, messageType) for this peer, or
    // allocates a free one. Returns nullptr only if every slot is already used
    // by a different stream (safety net for a future protocol growing past
    // kMaxTelemetryStreamsPerPeer known streams); in that case the caller
    // accepts the packet without sequence tracking, matching
    // ESPTelemetry::Decoder's existing fallback behavior for the same
    // situation.
    static TelemetryStreamState *findOrAllocateStream(TelemetryPeer &peer, uint8_t version, uint8_t messageType)
    {
        TelemetryStreamState *freeSlot = nullptr;
        for (size_t i = 0; i < kMaxTelemetryStreamsPerPeer; ++i)
        {
            TelemetryStreamState &slot = peer.streams[i];
            if (slot.used && slot.version == version && slot.messageType == messageType)
            {
                return &slot;
            }
            if (!slot.used && freeSlot == nullptr)
            {
                freeSlot = &slot;
            }
        }
        return freeSlot;
    }

    TelemetryPeer peers_[kDefaultTelemetryPeerCapacity]{};
    size_t count_ = 0;
    uint32_t staleTimeoutMs_ = 5000;
    uint32_t unknownSenderRejects_ = 0;
    uint32_t identityMismatchRejects_ = 0;
    bool configConflict_ = false;
};

} // namespace ESPTelemetry

#endif
