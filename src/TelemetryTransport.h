#ifndef ESP_TELEMETRY_TRANSPORT_H
#define ESP_TELEMETRY_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

namespace ESPTelemetry
{

enum class TelemetryTransportHealth : uint8_t
{
    Unknown,
    Ready,
    Degraded,
    Failed
};

struct TelemetryTransportStatistics
{
    uint32_t packetsSent;
    uint32_t packetsReceived;
    uint32_t bytesSent;
    uint32_t bytesReceived;
    uint32_t sendFailures;
    uint32_t receiveFailures;
    uint32_t sendCallbacks;
    uint32_t sendCallbackFailures;
    uint32_t receiveDropped;
    uint32_t unknownSenderRejects;
    uint32_t identityMismatchRejects;
    uint32_t lastSendAgeMs;
    uint32_t lastReceiveAgeMs;
    uint32_t lastSendCallbackLatencyMs;
    int8_t lastRssi;
    bool peerReady;
};

class ITelemetryTransport
{
public:
    virtual ~ITelemetryTransport() = default;

    virtual bool begin() = 0;
    virtual bool send(const uint8_t *data, size_t length) = 0;
    virtual size_t available() = 0;
    virtual size_t receive(uint8_t *buffer, size_t capacity) = 0;
    virtual TelemetryTransportStatistics statistics() const = 0;
    virtual TelemetryTransportHealth health() const = 0;
};

} // namespace ESPTelemetry

#endif
