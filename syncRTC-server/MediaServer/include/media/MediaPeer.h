#pragma once

#include <cstdint>
#include <memory>

class MediaSession;

class MediaPeer
{
public:
    MediaPeer(std::uint64_t meeting_id, int uid);

    std::uint64_t GetMeetingId() const;
    int GetUid() const;

    void SetSession(std::shared_ptr<MediaSession> session);
    std::shared_ptr<MediaSession> GetSession() const;

private:
    std::uint64_t m_meeting_id;
    int m_uid;
    std::shared_ptr<MediaSession> m_session;
};
