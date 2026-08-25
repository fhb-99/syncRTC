#include "media/MediaRoom.h"

#include "media/MediaPeer.h"

MediaRoom::MediaRoom(std::uint64_t meeting_id)
    : m_meeting_id(meeting_id)
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

    m_peers[peer->GetUid()] = std::move(peer);
}

void MediaRoom::RemovePeer(int uid)
{
    m_peers.erase(uid);
}

std::shared_ptr<MediaPeer> MediaRoom::GetPeer(int uid) const
{
    const auto iter = m_peers.find(uid);
    if (iter == m_peers.end()) {
        return nullptr;
    }
    return iter->second;
}

std::size_t MediaRoom::PeerCount() const
{
    return m_peers.size();
}
