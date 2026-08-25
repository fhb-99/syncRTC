#pragma once

#include "common/data.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rtc {
class PeerConnection;
}

class MediaSession
{
public:
    using SignalCallback = std::function<void(const MediaSignalResponse& response)>;

    MediaSession(std::uint64_t meeting_id, int uid, SignalCallback signal_callback);

    std::uint64_t GetMeetingId() const;
    int GetUid() const;

    void SetRemoteOffer(std::string sdp, std::uint64_t signal_id);
    void AddRemoteCandidate(std::string candidate, std::string mid);

private:
    std::uint64_t m_meeting_id;
    int m_uid;
    std::uint64_t m_signal_id;
    std::string m_remote_offer;
    SignalCallback m_signal_callback;
    std::shared_ptr<rtc::PeerConnection> m_peer_connection;
};
