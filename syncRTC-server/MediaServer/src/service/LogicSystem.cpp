#include "service/LogicSystem.h"

#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"
#include "net/Session.h"

#include <json/json.h>

#include <utility>

LogicSystem::LogicSystem()
    : m_stop(false),
      m_work_thread(&LogicSystem::DealMessage, this)
{
}

LogicSystem::~LogicSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cond.notify_one();
    if (m_work_thread.joinable()) {
        m_work_thread.join();
    }
}

void LogicSystem::PostMsgToQue(std::shared_ptr<MediaSignalNode> message)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_msg_que.push(std::move(message));
    }
    m_cond.notify_one();
}

void LogicSystem::DealMessage()
{
    while (true) {
        std::shared_ptr<MediaSignalNode> message;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cond.wait(lock, [this]() {
                return m_stop || !m_msg_que.empty();
            });

            if (m_stop && m_msg_que.empty()) {
                return;
            }

            message = std::move(m_msg_que.front());
            m_msg_que.pop();
        }

        // m_rooms 只在这个线程读写，offer、answer、candidate 会按 UDS 到达顺序串行处理。
        HandleSignal(message->session, std::move(message->message));
    }
}

void LogicSystem::HandleSignal(std::shared_ptr<Session> session, std::string message)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["signal_id"].isUInt64() || !root["meeting_id"].isUInt64() ||
        !root["uid"].isInt() || !root["signal_type"].isString()) {
        return;
    }

    MediaSignalRequest request;
    request.signal_id = root["signal_id"].asUInt64();
    request.meeting_id = root["meeting_id"].asUInt64();
    request.uid = root["uid"].asInt();
    request.signal_type = root["signal_type"].asString();
    if (request.signal_type == "offer" && root["sdp"].isString()) {
        request.sdp = root["sdp"].asString();
        HandleOffer(std::move(session), request);
        return;
    }
    if (request.signal_type == "answer" && root["sdp"].isString()) {
        request.sdp = root["sdp"].asString();
        HandleAnswer(request);
        return;
    }
    if (request.signal_type == "candidate" && root["candidate"].isString() && root["mid"].isString()) {
        request.candidate = root["candidate"].asString();
        request.mid = root["mid"].asString();
        HandleCandidate(std::move(session), request);
    }
}

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
        const std::weak_ptr<MediaRoom> media_room = room;
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
            },
            [media_room](int publisher_uid, const std::string& media_type) {
                const auto current_room = media_room.lock();
                if (!current_room) {
                    return;
                }

                // 客户端发布 Track 建立后，通知所属媒体房间准备其他成员的消费 Track。
                current_room->RegisterPublisherTrack(publisher_uid, media_type);
            },
            [media_room](int publisher_uid, const std::string& media_type, rtc::binary packet) {
                const auto current_room = media_room.lock();
                if (!current_room) {
                    return;
                }

                // RTP 不经过 RealtimeServer，直接在 MediaServer 的运行时房间内选路和转发。
                current_room->ForwardRtp(publisher_uid, media_type, std::move(packet));
            });
        peer->SetSession(media_session);
    }

    media_session->SetRemoteOffer(request.sdp, request.signal_id);
}

void LogicSystem::HandleAnswer(const MediaSignalRequest& request)
{
    const auto room_it = m_rooms.find(request.meeting_id);
    if (room_it == m_rooms.end()) {
        return;
    }

    const auto peer = room_it->second->GetPeer(request.uid);
    if (!peer || !peer->GetSession()) {
        return;
    }

    // 客户端 Answer 对应 MediaServer 主动发出的新成员订阅 Offer。
    // 设置后 PeerConnection 回到 Stable，若期间还有新成员加入，会继续生成下一轮 Offer。
    peer->GetSession()->SetRemoteAnswer(request.sdp);
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
