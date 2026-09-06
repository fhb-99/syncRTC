#pragma once

#include "common/data.h"

#include <rtc/rtc.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class MediaSession
{
public:
    using SignalCallback = std::function<void(const MediaSignalResponse& response)>;
    using TrackReadyCallback = std::function<void(int uid, const std::string& media_type)>;
    using MediaPacketCallback =
        std::function<void(int uid, const std::string& media_type, rtc::binary packet)>;

    MediaSession(std::uint64_t meeting_id,
                 int uid,
                 SignalCallback signal_callback,
                 TrackReadyCallback track_ready_callback,
                 MediaPacketCallback media_packet_callback);

    std::uint64_t GetMeetingId() const;
    int GetUid() const;

    void SetRemoteOffer(std::string sdp, std::uint64_t signal_id);
    void SetRemoteAnswer(std::string sdp);
    void AddRemoteCandidate(std::string candidate, std::string mid);
    void AddOutgoingTrack(int publisher_uid, const std::string& media_type, std::uint32_t ssrc);
    void RequestRenegotiation();
    void ForwardRtp(int publisher_uid, const std::string& media_type, const rtc::binary& packet);

private:
    struct OutgoingTrack
    {
        std::shared_ptr<rtc::Track> track;
        std::uint32_t ssrc;
    };

    std::uint64_t m_meeting_id;
    int m_uid;
    std::uint64_t m_signal_id;
    std::string m_remote_offer;
    SignalCallback m_signal_callback;
    TrackReadyCallback m_track_ready_callback;
    MediaPacketCallback m_media_packet_callback;
    std::shared_ptr<rtc::PeerConnection> m_peer_connection;
    std::shared_ptr<rtc::Track> m_incoming_video_track;
    std::shared_ptr<rtc::Track> m_incoming_audio_track;
    std::unordered_map<int, OutgoingTrack> m_outgoing_video_tracks;
    std::unordered_map<int, OutgoingTrack> m_outgoing_audio_tracks;
    std::mutex m_track_mutex;
    bool m_initial_answer_created = false;
    bool m_waiting_remote_answer = false;
    bool m_negotiation_pending = false;
    std::mutex m_negotiation_mutex;
};
