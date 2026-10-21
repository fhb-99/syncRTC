#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class MediaPeer;
class MediaSession;
class Session;

class MediaRoom
{
    friend struct TrackReadyTestAccess;
public:
    explicit MediaRoom(std::uint64_t meeting_id);

    std::uint64_t GetMeetingId() const;
    void AddPeer(std::shared_ptr<MediaPeer> peer);
    // expected_session_id 非零时，仅移除匹配的实例，避免旧会话清理误删同 UID 的新成员。
    void RemovePeer(int uid, std::uint64_t expected_session_id = 0);
    std::shared_ptr<MediaPeer> GetPeer(int uid) const;
    std::size_t PeerCount() const;
    // 锁内取得属于指定控制连接的会话快照，调用方在锁外逐个清理。
    std::vector<std::shared_ptr<MediaSession>> GetSessionsForConnection(
        const std::shared_ptr<Session>& connection) const;
    void RegisterPublisherTrack(int publisher_uid, const std::string& media_type);
    void ForwardRtp(int publisher_uid, std::uint64_t publisher_session_id,
                    const std::string& media_type, rtc::binary packet);

private:
    // MediaRoom 是媒体层运行时房间，不等同于 Redis 中的业务房间成员集合。
    std::uint64_t m_meeting_id;
    std::uint32_t m_next_ssrc;
    std::unordered_map<int, std::shared_ptr<MediaPeer>> m_peers;
    std::unordered_map<int, std::uint32_t> m_video_publishers;
    std::unordered_map<int, std::uint32_t> m_audio_publishers;
    mutable std::mutex m_mutex;
};
