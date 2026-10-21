#pragma once

#include "common/data.h"

#include <rtc/rtc.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class Session;

class MediaSession : public std::enable_shared_from_this<MediaSession>
{
    friend struct LocalSignalTestAccess;
    friend struct RtpSessionTestAccess;
public:
    using SignalCallback =
        std::function<void(std::uint64_t session_id, MediaSignalResponse response)>;
    using TrackReadyCallback =
        std::function<void(int uid, std::uint64_t session_id, const std::string& media_type)>;
    using MediaPacketCallback =
        std::function<void(int uid, std::uint64_t session_id,
                           const std::string& media_type, rtc::binary packet)>;

    MediaSession(std::uint64_t meeting_id,
                 int uid,
                 std::uint64_t signal_id,
                 std::weak_ptr<Session> signal_session,
                 SignalCallback signal_callback,
                 TrackReadyCallback track_ready_callback,
                 MediaPacketCallback media_packet_callback);

    std::uint64_t GetMeetingId() const;
    int GetUid() const;
    std::uint64_t GetInstanceId() const;
    std::uint64_t GetSignalId() const;
    std::shared_ptr<Session> GetSignalSession() const;

    void SetRemoteOffer(std::string sdp);
    void SetRemoteAnswer(std::string sdp);
    void AddRemoteCandidate(std::string candidate, std::string mid);
    void AddOutgoingTrack(int publisher_uid, std::uint64_t publisher_session_id,
                          const std::string& media_type, std::uint32_t ssrc);
    void RemoveOutgoingTracks(int publisher_uid);
    void RequestRenegotiation();
    void ForwardRtp(int publisher_uid, std::uint64_t publisher_session_id,
                    const std::string& media_type, const rtc::binary& packet);

private:
    struct OutgoingTrack
    {
        std::shared_ptr<rtc::Track> track;
        std::uint32_t ssrc;
        // UID 可以重用，消费 Track 必须绑定创建它时的发布者会话实例。
        std::uint64_t publisher_session_id;
    };

    std::uint64_t m_meeting_id;
    int m_uid;
    // 进程内每次新建会话都分配新标识；同一 UID 离会重入后，旧回调不能作用于新会话。
    const std::uint64_t m_instance_id;
    // 一次客户端初始 Offer 对应一个媒体会话，后续服务端 Offer 沿用该编号。
    // 新编号由 LogicSystem 创建新会话承接，异步回调不能读取可变的“最新编号”。
    const std::uint64_t m_signal_id;
    // 固定归属创建它的 UDS 连接；弱引用不保活断开连接，新连接须创建新的媒体会话。
    const std::weak_ptr<Session> m_signal_session;
    std::string m_remote_offer;
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
