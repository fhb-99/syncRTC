#include "media/MediaSession.h"

#include <rtc/rtc.hpp>

#include <utility>

MediaSession::MediaSession(std::uint64_t meeting_id, int uid, SignalCallback signal_callback)
    : m_meeting_id(meeting_id),
      m_uid(uid),
      m_signal_id(0),
      m_signal_callback(std::move(signal_callback)),
      m_peer_connection(std::make_shared<rtc::PeerConnection>())
{
    m_peer_connection->onLocalDescription([this](rtc::Description description) {
        MediaSignalResponse response;
        response.signal_id = m_signal_id;
        response.meeting_id = m_meeting_id;
        response.uid = m_uid;
        response.signal_type = description.typeString();
        response.sdp = std::string(description);
        m_signal_callback(response);
    });
    m_peer_connection->onLocalCandidate([this](rtc::Candidate candidate) {
        MediaSignalResponse response;
        response.signal_id = m_signal_id;
        response.meeting_id = m_meeting_id;
        response.uid = m_uid;
        response.signal_type = "candidate";
        response.candidate = std::string(candidate);
        response.mid = candidate.mid();
        m_signal_callback(response);
    });
}

std::uint64_t MediaSession::GetMeetingId() const
{
    return m_meeting_id;
}

int MediaSession::GetUid() const
{
    return m_uid;
}

void MediaSession::SetRemoteOffer(std::string sdp, std::uint64_t signal_id)
{
    m_signal_id = signal_id;
    m_remote_offer = std::move(sdp);
    // libdatachannel 接收 offer 后自动生成本地 answer，并通过 onLocalDescription 回调返回。
    m_peer_connection->setRemoteDescription(rtc::Description(m_remote_offer, "offer"));
}

void MediaSession::AddRemoteCandidate(std::string candidate, std::string mid)
{
    m_peer_connection->addRemoteCandidate(rtc::Candidate(candidate, mid));
}
