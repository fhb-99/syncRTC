#pragma once

#include <rtc/rtc.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

class MediaPeer;

class MediaRoom
{
public:
    explicit MediaRoom(std::uint64_t meeting_id);

    std::uint64_t GetMeetingId() const;
    void AddPeer(std::shared_ptr<MediaPeer> peer);
    void RemovePeer(int uid);
    std::shared_ptr<MediaPeer> GetPeer(int uid) const;
    std::size_t PeerCount() const;
    void RegisterPublisherTrack(int publisher_uid, const std::string& media_type);
    void ForwardRtp(int publisher_uid, const std::string& media_type, rtc::binary packet);

private:
    // MediaRoom 是媒体层运行时房间，不等同于 Redis 中的业务房间成员集合。
    std::uint64_t m_meeting_id;
    std::uint32_t m_next_ssrc;
    std::unordered_map<int, std::shared_ptr<MediaPeer>> m_peers;
    std::unordered_map<int, std::uint32_t> m_video_publishers;
    std::unordered_map<int, std::uint32_t> m_audio_publishers;
    mutable std::mutex m_mutex;
};
