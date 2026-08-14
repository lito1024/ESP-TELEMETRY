#ifndef ESP_TELEMETRY_ESPNOW_TRANSPORT_H
#define ESP_TELEMETRY_ESPNOW_TRANSPORT_H

#include "TelemetryPeerManager.h"
#include "TelemetryTransport.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_err.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <string.h>

namespace ESPTelemetry
{

struct ESPNowReceivedPacket
{
    uint8_t senderMac[6];
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    size_t length;
    int8_t rssi;
    uint32_t receivedMs;
};

class ESPNowTransport : public ITelemetryTransport
{
public:
    ESPNowTransport(const uint8_t *peerMac = nullptr, uint8_t channel = 0)
        : channel_(channel)
    {
        if (peerMac != nullptr)
        {
            memcpy(peerMac_, peerMac, sizeof(peerMac_));
            havePeer_ = true;
        }
    }

    void setPeerManager(TelemetryPeerManager *manager)
    {
        peerManager_ = manager;
    }

    PeerRegisterResult addTrustedPeer(const uint8_t *mac, uint8_t deviceId)
    {
        if (peerManager_ == nullptr)
        {
            return PeerRegisterResult::CapacityFull;
        }
        return peerManager_->addTrustedPeer(mac, deviceId);
    }

    bool begin() override
    {
        if (!initialized_)
        {
            const esp_err_t initResult = esp_now_init();
            if (initResult != ESP_OK && initResult != ESP_ERR_ESPNOW_INTERNAL)
            {
                health_ = TelemetryTransportHealth::Failed;
                return false;
            }
            initialized_ = true;
            callbacksRegistered_ = false;
        }

        instance_ = this;
        if (!callbacksRegistered_)
        {
            esp_now_register_send_cb(&ESPNowTransport::onSendComplete);
            esp_now_register_recv_cb(&ESPNowTransport::onReceive);
            callbacksRegistered_ = true;
        }

        if (havePeer_)
        {
            if (!registerEspNowPeer(peerMac_))
            {
                health_ = TelemetryTransportHealth::Failed;
                stats_.peerReady = false;
                return false;
            }
        }

        health_ = TelemetryTransportHealth::Ready;
        stats_.peerReady = havePeer_;
        return true;
    }

    void end()
    {
        if (initialized_)
        {
            esp_now_unregister_send_cb();
            esp_now_unregister_recv_cb();
            esp_now_deinit();
        }
        callbacksRegistered_ = false;
        initialized_ = false;
        stats_.peerReady = false;
        queuedLength_ = 0;
        health_ = TelemetryTransportHealth::Unknown;
        if (instance_ == this)
        {
            instance_ = nullptr;
        }
    }

    bool send(const uint8_t *data, size_t length) override
    {
        if (!havePeer_ || data == nullptr || length == 0 || length > ESP_NOW_MAX_DATA_LEN)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        sendStartedUs_ = nowUs();
        const esp_err_t result = esp_now_send(peerMac_, data, length);
        if (result != ESP_OK)
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

    // Stage 36: send to an explicit destination peer, distinct from the
    // single fixed peer_ configured at construction (used by Bridge to
    // route a command to whichever Gateway a device_id maps to, and by
    // Gateway to reply to whichever peer -- always the Bridge -- sent the
    // command). Registers the peer with ESP-NOW on demand if not already
    // present; does not affect the fixed-peer send() path above.
    bool sendTo(const uint8_t *peerMac, const uint8_t *data, size_t length)
    {
        if (peerMac == nullptr || data == nullptr || length == 0 || length > ESP_NOW_MAX_DATA_LEN)
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        if (!registerEspNowPeer(peerMac))
        {
            ++stats_.sendFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return false;
        }

        sendStartedUs_ = nowUs();
        const esp_err_t result = esp_now_send(peerMac, data, length);
        if (result != ESP_OK)
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
        return queuedLength_;
    }

    size_t receive(uint8_t *buffer, size_t capacity) override
    {
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

    bool receivePacket(ESPNowReceivedPacket &packet)
    {
        if (queuedLength_ == 0)
        {
            return false;
        }

        memcpy(packet.senderMac, queuedSenderMac_, sizeof(packet.senderMac));
        memcpy(packet.data, queuedFrame_, queuedLength_);
        packet.length = queuedLength_;
        packet.rssi = queuedRssi_;
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
        if (peerManager_ != nullptr)
        {
            copy.unknownSenderRejects = peerManager_->unknownSenderRejects();
            copy.identityMismatchRejects = peerManager_->identityMismatchRejects();
        }
        return copy;
    }
    TelemetryTransportHealth health() const override { return health_; }
    bool initialized() const { return initialized_; }
    bool peerReady() const { return stats_.peerReady; }

private:
    static uint32_t nowMs()
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    }

    static uint64_t nowUs()
    {
        return static_cast<uint64_t>(esp_timer_get_time());
    }

    static void onSendComplete(const esp_now_send_info_t *txInfo, esp_now_send_status_t status)
    {
        (void) txInfo;
        if (instance_ == nullptr)
        {
            return;
        }
        instance_->handleSendComplete(status);
    }

    static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen)
    {
        if (instance_ == nullptr)
        {
            return;
        }
        instance_->handleReceive(info, data, dataLen);
    }

    void handleSendComplete(esp_now_send_status_t status)
    {
        ++stats_.sendCallbacks;
        if (sendStartedUs_ != 0)
        {
            stats_.lastSendCallbackLatencyMs = static_cast<uint32_t>((nowUs() - sendStartedUs_) / 1000ULL);
        }
        if (status != ESP_NOW_SEND_SUCCESS)
        {
            ++stats_.sendCallbackFailures;
            health_ = TelemetryTransportHealth::Degraded;
            return;
        }
        health_ = TelemetryTransportHealth::Ready;
    }

    void handleReceive(const esp_now_recv_info_t *info, const uint8_t *data, int dataLen)
    {
        if (data == nullptr || dataLen <= 0)
        {
            ++stats_.receiveFailures;
            return;
        }
        if (static_cast<size_t>(dataLen) > sizeof(queuedFrame_))
        {
            ++stats_.receiveFailures;
            return;
        }
        if (info == nullptr || info->src_addr == nullptr)
        {
            ++stats_.receiveFailures;
            return;
        }
        if (peerManager_ != nullptr && peerManager_->findByMac(info->src_addr) == nullptr)
        {
            peerManager_->recordUnknownSender();
            ++stats_.receiveDropped;
            ++stats_.receiveFailures;
            return;
        }
        if (queuedLength_ != 0)
        {
            ++stats_.receiveDropped;
        }
        memcpy(queuedSenderMac_, info->src_addr, sizeof(queuedSenderMac_));
        memcpy(queuedFrame_, data, static_cast<size_t>(dataLen));
        queuedLength_ = static_cast<size_t>(dataLen);
        queuedReceivedMs_ = nowMs();
        queuedRssi_ = 0;
        ++stats_.packetsReceived;
        stats_.bytesReceived += static_cast<uint32_t>(dataLen);
        lastReceiveMs_ = queuedReceivedMs_;
        if (info != nullptr && info->rx_ctrl != nullptr)
        {
            queuedRssi_ = static_cast<int8_t>(info->rx_ctrl->rssi);
            stats_.lastRssi = queuedRssi_;
        }
        health_ = TelemetryTransportHealth::Ready;
    }

    bool registerEspNowPeer(const uint8_t *mac)
    {
        esp_now_peer_info_t peer{};
        memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
        peer.channel = channel_;
        peer.encrypt = false;
        if (esp_now_is_peer_exist(mac))
        {
            return true;
        }
        const esp_err_t result = esp_now_add_peer(&peer);
        return result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
    }

    static ESPNowTransport *instance_;
    TelemetryPeerManager *peerManager_ = nullptr;
    uint8_t peerMac_[6]{};
    uint8_t channel_ = 0;
    bool havePeer_ = false;
    bool initialized_ = false;
    bool callbacksRegistered_ = false;
    TelemetryTransportStatistics stats_{};
    TelemetryTransportHealth health_ = TelemetryTransportHealth::Unknown;
    uint8_t queuedSenderMac_[6]{};
    uint8_t queuedFrame_[ESP_NOW_MAX_DATA_LEN]{};
    volatile size_t queuedLength_ = 0;
    uint32_t queuedReceivedMs_ = 0;
    int8_t queuedRssi_ = 0;
    uint32_t lastSendMs_ = 0;
    uint32_t lastReceiveMs_ = 0;
    uint64_t sendStartedUs_ = 0;
};

inline ESPNowTransport *ESPNowTransport::instance_ = nullptr;

} // namespace ESPTelemetry

#endif

#endif
