#include "media/MediaPeer.h"

#include <utility>

MediaPeer::MediaPeer(std::uint64_t meeting_id, int uid)
    : m_meeting_id(meeting_id),
      m_uid(uid)
{
}

std::uint64_t MediaPeer::GetMeetingId() const
{
    return m_meeting_id;
}

int MediaPeer::GetUid() const
{
    return m_uid;
}

void MediaPeer::SetSession(std::shared_ptr<MediaSession> session)
{
    m_session = std::move(session);
}

std::shared_ptr<MediaSession> MediaPeer::GetSession() const
{
    return m_session;
}
