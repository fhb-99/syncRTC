#include "service/LogicSystem.h"

#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"
#include "net/Session.h"

#include <json/json.h>

#include <utility>

void LogicSystem::HandleOffer(std::shared_ptr<Session> session, const MediaSignalRequest& request)
{
    auto room_it = m_rooms.find(request.meeting_id);
    if (room_it == m_rooms.end()) {
        room_it = m_rooms.emplace(request.meeting_id,
                                  std::make_shared<MediaRoom>(request.meeting_id)).first;
    }

    const std::shared_ptr<MediaRoom>& room = room_it->second;
    auto peer = room->GetPeer(request.uid);
    if (!peer) {
        peer = std::make_shared<MediaPeer>(request.meeting_id, request.uid);
        room->AddPeer(peer);
    }

    auto media_session = peer->GetSession();
    if (!media_session) {
        const std::weak_ptr<Session> signal_session = session;
        media_session = std::make_shared<MediaSession>(
            request.meeting_id,
            request.uid,
            [signal_session](const MediaSignalResponse& response) {
                const auto current_session = signal_session.lock();
                if (!current_session) {
                    return;
                }

                Json::Value value;
                value["signal_id"] = static_cast<Json::UInt64>(response.signal_id);
                value["meeting_id"] = static_cast<Json::UInt64>(response.meeting_id);
                value["uid"] = response.uid;
                value["signal_type"] = response.signal_type;
                value["sdp"] = response.sdp;
                value["candidate"] = response.candidate;
                value["mid"] = response.mid;
                // libdatachannel 回调可能异步触发，Session::Send 负责串行写入这条 UDS 连接。
                current_session->Send(value.toStyledString());
        });
        peer->SetSession(media_session);
    }

    media_session->SetRemoteOffer(request.sdp, request.signal_id);
}

void LogicSystem::HandleCandidate(std::shared_ptr<Session>, const MediaSignalRequest& request)
{
    const auto room_it = m_rooms.find(request.meeting_id);
    if (room_it == m_rooms.end()) {
        return;
    }

    const auto peer = room_it->second->GetPeer(request.uid);
    if (!peer || !peer->GetSession()) {
        return;
    }
    peer->GetSession()->AddRemoteCandidate(request.candidate, request.mid);
}
