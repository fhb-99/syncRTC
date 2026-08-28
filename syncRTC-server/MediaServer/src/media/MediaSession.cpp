#include "media/MediaSession.h"

#include <rtc/rtc.hpp>

#include <mutex>
#include <utility>

MediaSession::MediaSession(std::uint64_t meeting_id,
                           int uid,
                           SignalCallback signal_callback,
                           TrackReadyCallback track_ready_callback,
                           MediaPacketCallback media_packet_callback)
    : m_meeting_id(meeting_id),
      m_uid(uid),
      m_signal_id(0),
      m_signal_callback(std::move(signal_callback)),
      m_track_ready_callback(std::move(track_ready_callback)),
      m_media_packet_callback(std::move(media_packet_callback))
{
    rtc::Configuration configuration;
    // 消费 Track 创建后需要通过后续的服务端 Offer 告知客户端。本阶段先关闭自动协商，
    // 避免 addTrack 立即生成一个 RealtimeServer 还不能转发的 Offer。
    configuration.disableAutoNegotiation = true;
    m_peer_connection = std::make_shared<rtc::PeerConnection>(configuration);

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

    m_peer_connection->onTrack([this](std::shared_ptr<rtc::Track> track) {
        const std::string media_type = track->description().type();
        if (media_type != "video" && media_type != "audio") {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_track_mutex);
            // 这里保存的是客户端发布给 MediaServer 的接收 Track。
            // 保存 shared_ptr 可以保证后续 RTP 到达时 Track 及其回调仍然有效。
            if (media_type == "video") {
                m_incoming_video_track = track;
            } else {
                m_incoming_audio_track = track;
            }
        }

        const int publisher_uid = m_uid;
        const MediaPacketCallback packet_callback = m_media_packet_callback;
        track->onMessage(
            [publisher_uid, media_type, packet_callback](rtc::binary packet) {
                // Track 的二进制回调既可能收到 RTP，也可能收到 RTCP。
                // 本阶段只做音视频 RTP 转发，质量反馈、丢包重传和关键帧请求后续再接入。
                if (rtc::IsRtcp(packet)) {
                    return;
                }

                // packet 仍是客户端完成编码、RTP 封装后的原始数据。
                // MediaServer 不解码、不转码，只把发布者身份和媒体类型交给房间完成选路。
                packet_callback(publisher_uid, media_type, std::move(packet));
            },
            nullptr);

        // 房间在确认发布 Track 存在后，为其他成员准备对应的发送 Track。
        // 这些 Track 本阶段只创建和保存，等后续 Offer/Answer 协商完成后才会进入 Open 状态。
        m_track_ready_callback(publisher_uid, media_type);
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
    // 先设置客户端 Offer，让 libdatachannel 根据其中的 audio/video m-line 创建接收 Track。
    m_peer_connection->setRemoteDescription(rtc::Description(m_remote_offer, "offer"));

    // 关闭自动协商后，需要显式生成本地 Answer。当前 Answer 只回应客户端原有的发布 Track；
    // MediaServer 新增的消费 Track 会保留在 PeerConnection 中，等待后续服务端 Offer 再协商。
    m_peer_connection->setLocalDescription();

    bool create_offer = false;
    {
        std::lock_guard<std::mutex> lock(m_negotiation_mutex);
        m_initial_answer_created = true;
        if (m_negotiation_pending) {
            m_negotiation_pending = false;
            m_waiting_remote_answer = true;
            create_offer = true;
        }
    }

    if (create_offer) {
        // 消费 Track 可能在处理客户端初始 Offer 时已经准备完成。
        // 必须先生成并发送初始 Answer，再在稳定状态下生成包含消费 Track 的新 Offer。
        m_peer_connection->setLocalDescription();
    }
}

void MediaSession::SetRemoteAnswer(std::string sdp)
{
    // 该 Answer 对应 MediaServer 主动发出的 Offer。设置后，新消费 Track 才真正完成协商，
    // DTLS/SRTP 连接建立后 Track 会进入 Open，已有 RTP 转发代码即可开始写包。
    m_peer_connection->setRemoteDescription(rtc::Description(std::move(sdp), "answer"));

    bool create_next_offer = false;
    {
        std::lock_guard<std::mutex> lock(m_negotiation_mutex);
        m_waiting_remote_answer = false;
        if (m_negotiation_pending) {
            // 等待 Answer 期间如果又有成员加入，只合并成下一轮协商，不制造并行 Offer。
            m_negotiation_pending = false;
            m_waiting_remote_answer = true;
            create_next_offer = true;
        }
    }

    if (create_next_offer) {
        m_peer_connection->setLocalDescription();
    }
}

void MediaSession::AddRemoteCandidate(std::string candidate, std::string mid)
{
    m_peer_connection->addRemoteCandidate(rtc::Candidate(candidate, mid));
}

void MediaSession::AddOutgoingTrack(int publisher_uid,
                                    const std::string& media_type,
                                    std::uint32_t ssrc)
{
    std::lock_guard<std::mutex> lock(m_track_mutex);
    auto& outgoing_tracks = media_type == "video"
                                ? m_outgoing_video_tracks
                                : m_outgoing_audio_tracks;
    if (outgoing_tracks.find(publisher_uid) != outgoing_tracks.end()) {
        return;
    }

    const std::string mid = media_type + "-" + std::to_string(publisher_uid);
    std::shared_ptr<rtc::Track> track;
    if (media_type == "video") {
        rtc::Description::Video video(mid, rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        video.addSSRC(ssrc, mid, "syncRTC", mid);
        track = m_peer_connection->addTrack(video);
    } else {
        rtc::Description::Audio audio(mid, rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        audio.addSSRC(ssrc, mid, "syncRTC", mid);
        track = m_peer_connection->addTrack(audio);
    }

    // publisher_uid 表示这条消费 Track 属于哪位发布者；同一接收者会为每位其他成员
    // 分别保存一对 audio/video Track，后续客户端才能按 MID 区分不同参会者。
    outgoing_tracks.emplace(publisher_uid, OutgoingTrack{std::move(track), ssrc});
}

void MediaSession::RequestRenegotiation()
{
    {
        std::lock_guard<std::mutex> lock(m_negotiation_mutex);
        if (!m_initial_answer_created || m_waiting_remote_answer) {
            // 初次 Answer 尚未生成，或者上一份服务端 Offer 仍在等待客户端 Answer 时，
            // 只记录“还需要再协商”。这样连续入会不会让同一 PeerConnection 同时存在多个 Offer。
            m_negotiation_pending = true;
            return;
        }
        m_waiting_remote_answer = true;
    }

    // PeerConnection 当前处于 Stable 状态，setLocalDescription() 会创建新的本地 Offer。
    // onLocalDescription 回调会沿用初次协商的 signal_id，经 UDS 交给 RealtimeServer 转发。
    m_peer_connection->setLocalDescription();
}

void MediaSession::ForwardRtp(int publisher_uid,
                              const std::string& media_type,
                              const rtc::binary& packet)
{
    OutgoingTrack outgoing_track;
    {
        std::lock_guard<std::mutex> lock(m_track_mutex);
        const auto& outgoing_tracks = media_type == "video"
                                          ? m_outgoing_video_tracks
                                          : m_outgoing_audio_tracks;
        const auto track_it = outgoing_tracks.find(publisher_uid);
        if (track_it == outgoing_tracks.end()) {
            return;
        }
        outgoing_track = track_it->second;
    }

    // 新增消费 Track 在完成后续 Offer/Answer 前不会 Open，此时跳过发送即可。
    // 协商完成后，相同的转发路径会直接开始写 RTP，无需再修改房间转发逻辑。
    if (!outgoing_track.track->isOpen()) {
        return;
    }

    rtc::binary forwarded_packet = packet;
    auto* rtp_header = reinterpret_cast<rtc::RtpHeader*>(forwarded_packet.data());
    // 多个客户端目前使用相同的采集端 SSRC。转发前改成房间为发布者分配的 SSRC，
    // 使接收端 SDP 中声明的 SSRC 与实际收到的 RTP 保持一致；序列号、时间戳和负载原样保留。
    rtp_header->setSsrc(outgoing_track.ssrc);
    outgoing_track.track->send(std::move(forwarded_packet));
}
