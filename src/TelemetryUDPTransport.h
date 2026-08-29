#ifndef ESP_TELEMETRY_UDP_TRANSPORT_H
#define ESP_TELEMETRY_UDP_TRANSPORT_H

#include "TelemetryTransport.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <esp_timer.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

namespace ESPTelemetry
{

// Stage 42A: shared default UDP port for all ESP-TELEMETRY IP-transport
// traffic. Every node that speaks ESP-TELEMETRY-over-UDP listens on this
// port and sends to this port on its peer's IP -- symmetric, no per-role
// port allocation, mirroring how ESP-NOW's implicit "same channel" needs
// no per-role configuration either. Individual deployments may override it
// (setLocalPort()/sendTo() both take an explicit port); one documented
// default keeps peer configuration down to "an IP address."
static constexpr uint16_t kDefaultTelemetryUdpPort = 50823;
static constexpr size_t kUdpMaxDatagramSize = 256;

struct UDPReceivedPacket
{
    uint32_t senderIp; // network-byte-order IPv4, same representation as sockaddr_in.sin_addr.s_addr
    uint16_t senderPort;
    uint8_t data[kUdpMaxDatagramSize];
    size_t length;
    uint32_t receivedMs;
};

// Stage 42A: UDP adapter for ITelemetryTransport, implemented directly on
// ESP-IDF/lwIP BSD sockets (not Arduino WiFiUDP) so it builds identically
// under both the pure ESP-IDF framework (sweet-power-controller,
// garage-sensor) and the Arduino framework (luxpower-gateway,
// telemetry-bridge) this project already targets -- matching the existing
// convention (garage_presence_espnow.cpp, esp_telemetry_espnow's ESP-NOW
// path) of using raw ESP-IDF networking APIs rather than Arduino wrappers.
//
// One ESP-TELEMETRY frame per UDP datagram -- no stream framing or
// reassembly (see stages/Stage42A.md "UDP Framing"): every send()/sendTo()
// call becomes exactly one sendto(), and every receivePacket() call
// returns at most one already-complete datagram.
//
// Trust/identity is NOT this class's job: it exposes the sender's source
// IP (the same way ESPNowTransport exposes the sender MAC) but performs no
// allowlist check itself -- callers validate source IP -> expected
// device_id explicitly (see stages/Stage42A.md "Trust Model").
class UDPTransport : public ITelemetryTransport
{
public:
    UDPTransport() = default;

    // 0 = do not bind (send-only use, e.g. a sensor node that only ever
    // calls sendTo()); a socket is still created in begin() so send()/
    // sendTo() work, it just never receives unsolicited datagrams.
    void setLocalPort(uint16_t localPort) { localPort_ = localPort; }

    // Optional fixed default destination for send(), mirroring
    // ESPNowTransport's constructor-supplied fixed peer. sendTo() below is
    // the primary path every Stage 42A sender actually uses.
    void setDefaultPeer(uint32_t peerIp, uint16_t peerPort)
    {
        peerIp_ = peerIp;
        peerPort_ = peerPort;
        havePeer_ = true;
    }

    bool begin() override
    {
        if (initialized_)
        {
            return true;
        }

        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_ < 0)
        {
            health_ = TelemetryTransportHealth::Failed;
            return false;
        }

        const int flags = fcntl(socket_, F_GETFL, 0);
        fcntl(socket_, F_SETFL, flags | O_NONBLOCK);

        if (localPort_ != 0)
        {
            sockaddr_in bindAddr{};
            bindAddr.sin_family = AF_INET;
            bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
            bindAddr.sin_port = htons(localPort_);
            if (::bind(socket_, reinterpret_cast<sockaddr *>(&bindAddr), sizeof(bindAddr)) != 0)
            {
                ::close(socket_);
                socket_ = -1;
                health_ = TelemetryTransportHealth::Failed;
                return false;
            }
        }

        initialized_ = true;
        health_ = TelemetryTransportHealth::Ready;
        return true;
    }

    void end()
    {
        if (socket_ >= 0)
        {
            ::close(socket_);
            socket_ = -1;
        }
        initialized_ = false;
        queuedLength_ = 0;
        health_ = TelemetryTransportHealth::Unknown;
    }

    bool send(const uint8_t *data, size_t length) override
    {
        if (!havePeer_)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }
        return sendTo(peerIp_, peerPort_, data, length);
    }

    // Stage 42A: explicit-destination send (mirrors Stage 36's
    // ESPNowTransport::sendTo()) -- every Stage 42A sender uses this, not
    // the fixed-peer send() above.
    bool sendTo(uint32_t peerIp, uint16_t peerPort, const uint8_t *data, size_t length)
    {
        if (!initialized_ || data == nullptr || length == 0 || length > kUdpMaxDatagramSize || peerIp == 0)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = peerIp;
        dest.sin_port = htons(peerPort);

        const int sent = sendto(socket_, data, length, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest));
        if (sent < 0 || static_cast<size_t>(sent) != length)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        ++stats_.packetsSent;
        stats_.bytesSent += static_cast<uint32_t>(length);
        lastSendMs_ = nowMs();
        health_ = TelemetryTransportHealth::Ready;
        return true;
    }

    size_t available() override
    {
        pollForDatagram_();
        return queuedLength_;
    }

    size_t receive(uint8_t *buffer, size_t capacity) override
    {
        pollForDatagram_();
        if (buffer == nullptr || capacity == 0)
        {
            ++stats_.receiveFailures;
            return 0;
        }
        if (queuedLength_ == 0)
        {
            return 0;
        }
        const size_t length = queuedLength_ < capacity ? queuedLength_ : capacity;
        memcpy(buffer, queuedFrame_, length);
        queuedLength_ = 0;
        return length;
    }

    // Stage 42A receiver path: exposes sender IP/port, the UDP analogue of
    // ESPNowTransport::receivePacket() exposing sender MAC. Callers (e.g.
    // esp_telemetry_espnow's UDP garage-presence path) validate senderIp
    // against an expected-peer allowlist themselves -- this class makes no
    // trust decision.
    bool receivePacket(UDPReceivedPacket &packet)
    {
        pollForDatagram_();
        if (queuedLength_ == 0)
        {
            return false;
        }
        packet.senderIp = queuedSenderIp_;
        packet.senderPort = queuedSenderPort_;
        memcpy(packet.data, queuedFrame_, queuedLength_);
        packet.length = queuedLength_;
        packet.receivedMs = queuedReceivedMs_;
        queuedLength_ = 0;
        return true;
    }

    TelemetryTransportStatistics statistics() const override
    {
        TelemetryTransportStatistics copy = stats_;
        const uint32_t now = nowMs();
        copy.lastSendAgeMs = lastSendMs_ == 0 ? 0xFFFFFFFFUL : now - lastSendMs_;
        copy.lastReceiveAgeMs = lastReceiveMs_ == 0 ? 0xFFFFFFFFUL : now - lastReceiveMs_;
        return copy;
    }
    TelemetryTransportHealth health() const override { return health_; }
    bool initialized() const { return initialized_; }

private:
    static uint32_t nowMs()
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    // Single-slot receive buffer, deliberately mirroring ESPNowTransport's
    // existing single-in-flight-packet design (see the open
    // ADDITIONAL_FUTURE_TASKS.md item on ESP-NOW's receive-buffer
    // overwrite behavior) -- but materially less risky here: an undrained
    // datagram simply stays queued in the kernel/lwIP socket receive
    // buffer (not overwritten) until the next poll, so back-to-back sends
    // arriving before a loop() drain are not silently lost the way a
    // callback-driven single-slot overwrite would lose them.
    void pollForDatagram_()
    {
        if (!initialized_ || queuedLength_ != 0)
        {
            return;
        }

        sockaddr_in from{};
        socklen_t fromLen = sizeof(from);
        const int received = recvfrom(socket_, queuedFrame_, sizeof(queuedFrame_), 0,
                                       reinterpret_cast<sockaddr *>(&from), &fromLen);
        if (received <= 0)
        {
            if (received < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
            {
                ++stats_.receiveFailures;
            }
            return;
        }

        queuedLength_ = static_cast<size_t>(received);
        queuedSenderIp_ = from.sin_addr.s_addr;
        queuedSenderPort_ = ntohs(from.sin_port);
        queuedReceivedMs_ = nowMs();
        ++stats_.packetsReceived;
        stats_.bytesReceived += static_cast<uint32_t>(received);
        lastReceiveMs_ = queuedReceivedMs_;
        health_ = TelemetryTransportHealth::Ready;
    }

    int socket_ = -1;
    uint16_t localPort_ = 0;
    uint32_t peerIp_ = 0;
    uint16_t peerPort_ = 0;
    bool havePeer_ = false;
    bool initialized_ = false;
    TelemetryTransportStatistics stats_{};
    TelemetryTransportHealth health_ = TelemetryTransportHealth::Unknown;
    uint8_t queuedFrame_[kUdpMaxDatagramSize]{};
    volatile size_t queuedLength_ = 0;
    uint32_t queuedSenderIp_ = 0;
    uint16_t queuedSenderPort_ = 0;
    uint32_t queuedReceivedMs_ = 0;
    uint32_t lastSendMs_ = 0;
    uint32_t lastReceiveMs_ = 0;
};

} // namespace ESPTelemetry

#endif

#endif
