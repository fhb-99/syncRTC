#include "media/MediaRoom.h"

#include "media/MediaPeer.h"
#include "media/MediaSession.h"

#include <atomic>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct OutgoingTrackBinding
{
    std::shared_ptr<MediaSession> session;
    int publisher_uid;
    std::uint64_t publisher_session_id;
    std::string media_type;
    std::uint32_t ssrc;
};

} // namespace

MediaRoom::MediaRoom(std::uint64_t meeting_id)
    : m_meeting_id(meeting_id),
      m_next_ssrc(100000)
{
}

std::uint64_t MediaRoom::GetMeetingId() const
{
    return m_meeting_id;
}

void MediaRoom::AddPeer(std::shared_ptr<MediaPeer> peer)
{
    if (!peer) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_peers[peer->GetUid()] = std::move(peer);
}

void MediaRoom::RemovePeer(int uid, std::uint64_t expected_session_id)
{
    std::vector<std::shared_ptr<MediaSession>> remaining_sessions;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto peer = m_peers.find(uid);
        if (expected_session_id != 0 &&
            (peer == m_peers.end() || !peer->second->GetSession() ||
             peer->second->GetSession()->GetInstanceId() != expected_session_id)) {
            return;
        }
        m_peers.erase(uid);
        m_video_publishers.erase(uid);
        m_audio_publishers.erase(uid);

        for (const auto& item : m_peers) {
            if (item.second->GetSession()) {
                remaining_sessions.push_back(item.second->GetSession());
            }
        }
    }

    // 清理其他成员保存的该发布者发送 Track，允许同一用户重新入会后重新创建。
    for (const auto& session : remaining_sessions) {
        session->RemoveOutgoingTracks(uid);
    }
}

std::shared_ptr<MediaPeer> MediaRoom::GetPeer(int uid) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto iter = m_peers.find(uid);
    if (iter == m_peers.end()) {
        return nullptr;
    }
    return iter->second;
}

std::size_t MediaRoom::PeerCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_peers.size();
}

std::vector<std::shared_ptr<MediaSession>> MediaRoom::GetSessionsForConnection(
    const std::shared_ptr<Session>& connection) const
{
    std::vector<std::shared_ptr<MediaSession>> sessions;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& [uid, peer] : m_peers) {
        const auto session = peer->GetSession();
        if (session && session->GetSignalSession() == connection) {
            sessions.push_back(session);
        }
    }
    return sessions;
}

void MediaRoom::RegisterPublisherTrack(int publisher_uid, const std::string& media_type)
{
    std::vector<OutgoingTrackBinding> tracks_to_add;
    std::vector<std::shared_ptr<MediaSession>> sessions_to_negotiate;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto publisher_peer = m_peers.find(publisher_uid);
        if (publisher_peer == m_peers.end() || !publisher_peer->second->GetSession()) {
            return;
        }
        const auto publisher_session_id = publisher_peer->second->GetSession()->GetInstanceId();
        auto& publishers = media_type == "video" ? m_video_publishers : m_audio_publishers;

        // 每个发布者的每种媒体只登记一次。音视频使用不同 SSRC，且同一发布流
        // 在所有接收者侧保持相同 SSRC，便于 SDP 和后续 RTP 转发保持一致。
        if (publishers.find(publisher_uid) != publishers.end()) {
            return;
        }
        publishers.emplace(publisher_uid, m_next_ssrc++);

        const auto video_it = m_video_publishers.find(publisher_uid);
        const auto audio_it = m_audio_publishers.find(publisher_uid);
        if (video_it == m_video_publishers.end() || audio_it == m_audio_publishers.end()) {
            // 客户端初始 Offer 固定包含 H264 和 Opus。等两条发布 Track 都建立后再一次性
            // 创建消费 Track，只触发一轮包含音频和视频的服务端 Offer。
            return;
        }

        for (const auto& [target_uid, target_peer] : m_peers) {
            if (target_uid == publisher_uid || !target_peer->GetSession()) {
                continue;
            }

            // 已在会议中的成员需要同时增加新发布者的 H264 和 Opus 消费 Track。
            tracks_to_add.push_back(
                {target_peer->GetSession(), publisher_uid, publisher_session_id, "video", video_it->second});
            tracks_to_add.push_back(
                {target_peer->GetSession(), publisher_uid, publisher_session_id, "audio", audio_it->second});
            sessions_to_negotiate.push_back(target_peer->GetSession());
        }

        const auto publisher_peer_it = m_peers.find(publisher_uid);
        if (publisher_peer_it != m_peers.end() && publisher_peer_it->second->GetSession()) {
            const auto publisher_session = publisher_peer_it->second->GetSession();
            bool needs_negotiation = false;
            for (const auto& [existing_uid, existing_video_ssrc] : m_video_publishers) {
                if (existing_uid == publisher_uid) {
                    continue;
                }

                const auto existing_audio_it = m_audio_publishers.find(existing_uid);
                if (existing_audio_it == m_audio_publishers.end()) {
                    continue;
                }

                // 新成员也要一次性订阅会议中已经存在成员的 H264 和 Opus Track。
                // 发布表和成员表在同一房间锁下维护，登记的发布者必有对应的媒体会话。
                const auto existing_session_id = m_peers.at(existing_uid)->GetSession()->GetInstanceId();
                tracks_to_add.push_back(
                    {publisher_session, existing_uid, existing_session_id, "video", existing_video_ssrc});
                tracks_to_add.push_back(
                    {publisher_session, existing_uid, existing_session_id, "audio", existing_audio_it->second});
                needs_negotiation = true;
            }
            if (needs_negotiation) {
                sessions_to_negotiate.push_back(std::move(publisher_session));
            }
        }
    }

    // addTrack 可能进入 libdatachannel 内部状态机，所以离开房间锁后再执行，
    // 避免媒体库回调反向访问 MediaRoom 时形成锁等待。
    for (const auto& binding : tracks_to_add) {
        // 前面的操作可能已清理失败成员，跳过涉及已移除来源或接收者的剩余绑定。
        const auto target = GetPeer(binding.session->GetUid());
        const auto source = GetPeer(binding.publisher_uid);
        if (!target || target->GetSession() != binding.session || !source ||
            !source->GetSession() ||
            source->GetSession()->GetInstanceId() != binding.publisher_session_id) {
            continue;
        }
        try {
            binding.session->AddOutgoingTrack(
                binding.publisher_uid, binding.publisher_session_id, binding.media_type, binding.ssrc);
        }
        catch (const std::exception& error) {
            // 失败属于正在操作的接收会话，不能按触发 Track 就绪通知的发布者 UID 清理。
            std::cerr << "[media-control-error] type=add-track meeting=" << m_meeting_id
                      << " uid=" << binding.session->GetUid()
                      << " instance=" << binding.session->GetInstanceId()
                      << " error=" << error.what() << std::endl;
            RemovePeer(binding.session->GetUid(), binding.session->GetInstanceId());
        }
    }

    // 必须先把本轮所有消费 Track 加入 PeerConnection，再生成 Offer，
    // 这样一份 SDP 就能完整描述本次新增的音频和视频 m-line。
    for (const auto& session : sessions_to_negotiate) {
        const auto peer = GetPeer(session->GetUid());
        if (!peer || peer->GetSession() != session) {
            continue;
        }
        try {
            session->RequestRenegotiation();
        }
        catch (const std::exception& error) {
            std::cerr << "[media-control-error] type=renegotiate meeting=" << m_meeting_id
                      << " uid=" << session->GetUid()
                      << " instance=" << session->GetInstanceId()
                      << " error=" << error.what() << std::endl;
            RemovePeer(session->GetUid(), session->GetInstanceId());
        }
    }
}

void MediaRoom::ForwardRtp(int publisher_uid,
                           std::uint64_t publisher_session_id,
                           const std::string& media_type,
                           rtc::binary packet)
{
    std::vector<std::shared_ptr<MediaSession>> target_sessions;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // 与 RemovePeer/AddPeer 共用房间锁。旧 Track 的回调即使仍存活，也不能冒充同 UID 的新会话。
        const auto publisher = m_peers.find(publisher_uid);
        if (publisher == m_peers.end() || !publisher->second->GetSession() ||
            publisher->second->GetSession()->GetInstanceId() != publisher_session_id) {
            return;
        }
        for (const auto& [target_uid, target_peer] : m_peers) {
            if (target_uid == publisher_uid || !target_peer->GetSession()) {
                continue;
            }
            target_sessions.push_back(target_peer->GetSession());
        }
    }

    if (target_sessions.empty()) {
        static std::atomic<std::uint64_t> dropped{0};
        const std::uint64_t count = ++dropped;
        if (count == 1 || count % 300 == 0) {
            std::cout << "[rtp-forward-skip]"
                      << " reason=no-target-session"
                      << " meeting=" << m_meeting_id
                      << " publisher=" << publisher_uid
                      << " media=" << media_type
                      << " dropped=" << count
                      << std::endl;
        }
        return;
    }

    // 同一份发布者 RTP 依次交给会议中的其他成员。MediaSession 会复制数据包、
    // 改写该发布者在房间中的 SSRC，并写入目标成员已经协商完成的发送 Track。
    // 网络发送不持有房间锁，某个客户端的发送速度不会阻塞成员表的查询和更新。
    for (const auto& target_session : target_sessions) {
        target_session->ForwardRtp(publisher_uid, publisher_session_id, media_type, packet);
    }
}
