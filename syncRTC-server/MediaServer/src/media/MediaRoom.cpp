#include "media/MediaRoom.h"

#include "media/MediaPeer.h"
#include "media/MediaSession.h"

#include <string>
#include <utility>
#include <vector>

namespace {

struct OutgoingTrackBinding
{
    std::shared_ptr<MediaSession> session;
    int publisher_uid;
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

void MediaRoom::RemovePeer(int uid)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_peers.erase(uid);
    m_video_publishers.erase(uid);
    m_audio_publishers.erase(uid);
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

void MediaRoom::RegisterPublisherTrack(int publisher_uid, const std::string& media_type)
{
    std::vector<OutgoingTrackBinding> tracks_to_add;
    std::vector<std::shared_ptr<MediaSession>> sessions_to_negotiate;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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
                {target_peer->GetSession(), publisher_uid, "video", video_it->second});
            tracks_to_add.push_back(
                {target_peer->GetSession(), publisher_uid, "audio", audio_it->second});
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
                tracks_to_add.push_back(
                    {publisher_session, existing_uid, "video", existing_video_ssrc});
                tracks_to_add.push_back(
                    {publisher_session, existing_uid, "audio", existing_audio_it->second});
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
        binding.session->AddOutgoingTrack(
            binding.publisher_uid, binding.media_type, binding.ssrc);
    }

    // 必须先把本轮所有消费 Track 加入 PeerConnection，再生成 Offer，
    // 这样一份 SDP 就能完整描述本次新增的音频和视频 m-line。
    for (const auto& session : sessions_to_negotiate) {
        session->RequestRenegotiation();
    }
}

void MediaRoom::ForwardRtp(int publisher_uid,
                           const std::string& media_type,
                           rtc::binary packet)
{
    std::vector<std::shared_ptr<MediaSession>> target_sessions;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& [target_uid, target_peer] : m_peers) {
            if (target_uid == publisher_uid || !target_peer->GetSession()) {
                continue;
            }
            target_sessions.push_back(target_peer->GetSession());
        }
    }

    // 同一份发布者 RTP 依次交给会议中的其他成员。MediaSession 会复制数据包、
    // 改写该发布者在房间中的 SSRC，并写入目标成员已经协商完成的发送 Track。
    // 网络发送不持有房间锁，某个客户端的发送速度不会阻塞成员表的查询和更新。
    for (const auto& target_session : target_sessions) {
        target_session->ForwardRtp(publisher_uid, media_type, packet);
    }
}
