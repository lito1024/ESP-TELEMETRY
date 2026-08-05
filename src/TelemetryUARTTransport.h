#ifndef ESP_TELEMETRY_UART_TRANSPORT_H
#define ESP_TELEMETRY_UART_TRANSPORT_H

#include "TelemetryTransport.h"

#if defined(ARDUINO)
#include <Stream.h>

namespace ESPTelemetry
{

class UARTTransport : public ITelemetryTransport
{
public:
    explicit UARTTransport(Stream &stream) : stream_(stream) {}

    bool begin() override
    {
        health_ = TelemetryTransportHealth::Ready;
        return true;
    }

    bool send(const uint8_t *data, size_t length) override
    {
        if (data == nullptr || length == 0)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        const size_t written = stream_.write(data, length);
        stream_.flush();
        stats_.bytesSent += static_cast<uint32_t>(written);
        if (written != length)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        ++stats_.packetsSent;
        health_ = TelemetryTransportHealth::Ready;
        return true;
    }

    size_t available() override
    {
        const int count = stream_.available();
        return count > 0 ? static_cast<size_t>(count) : 0;
    }

    size_t receive(uint8_t *buffer, size_t capacity) override
    {
        if (buffer == nullptr || capacity == 0)
        {
            ++stats_.receiveFailures;
            return 0;
        }

        size_t count = 0;
        while (count < capacity && stream_.available() > 0)
        {
            const int value = stream_.read();
            if (value < 0)
            {
                break;
            }
            buffer[count++] = static_cast<uint8_t>(value);
        }
        if (count > 0)
        {
            ++stats_.packetsReceived;
            stats_.bytesReceived += static_cast<uint32_t>(count);
        }
        return count;
    }

    TelemetryTransportStatistics statistics() const override { return stats_; }
    TelemetryTransportHealth health() const override { return health_; }

private:
    Stream &stream_;
    TelemetryTransportStatistics stats_{};
    TelemetryTransportHealth health_ = TelemetryTransportHealth::Unknown;
};

} // namespace ESPTelemetry

#endif

#endif
