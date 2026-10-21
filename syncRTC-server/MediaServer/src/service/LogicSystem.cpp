#include "service/LogicSystem.h"

#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"
#include "net/Session.h"

#include <json/json.h>

#include <exception>
#include <iostream>
#include <optional>
#include <utility>

namespace {

constexpr std::size_t kRoomWorkerCount = 4;

std::optional<MediaSignalRequest> ParseMediaSignalRequest(const std::string& message)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["signal_id"].isUInt64() || !root["meeting_id"].isUInt64() ||
        !root["uid"].isInt() || !root["signal_type"].isString()) {
        return std::nullopt;
    }

    MediaSignalRequest request;
    request.signal_id = root["signal_id"].asUInt64();
    request.meeting_id = root["meeting_id"].asUInt64();
    request.uid = root["uid"].asInt();
    request.signal_type = root["signal_type"].asString();

    if (request.signal_type == "offer" || request.signal_type == "answer") {
        if (!root["sdp"].isString()) {
            return std::nullopt;
        }
        request.sdp = root["sdp"].asString();
        return request;
    }
    if (request.signal_type == "candidate") {
        if (!root["candidate"].isString() || !root["mid"].isString()) {
            return std::nullopt;
        }
        request.candidate = root["candidate"].asString();
        request.mid = root["mid"].asString();
        return request;
    }
    if (request.signal_type == "leave") {
        return request;
    }
    return std::nullopt;
}

} // namespace

LogicSystem::LogicSystem()
    : m_room_workers(kRoomWorkerCount, [this](MediaRoomEvent event) {
          HandleRoomEvent(std::move(event));
      })
{
}

LogicSystem::~LogicSystem() = default;

void LogicSystem::PostMsgToQue(std::shared_ptr<Session> session, std::string message)
{
    auto request = ParseMediaSignalRequest(message);
    if (!request || !session || session->IsStopped()) {
        return;
    }

    const std::uint64_t meeting_id = request->meeting_id;
    // 必须先登记再入队：断开时房间可能尚未创建，不能只扫描当时的 m_rooms。
    session->RememberRoom(meeting_id);
    m_room_workers.Post(meeting_id, MediaSignalTask{std::move(session), std::move(*request)});
}

void LogicSystem::PostSignalDisconnected(const std::shared_ptr<Session>& session)
{
    if (!session || !session->IsStopped()) {
        return;
    }
    for (const auto meeting_id : session->GetMeetingIds()) {
        m_room_workers.Post(meeting_id, SignalDisconnectedEvent{meeting_id, session});
    }
}

void LogicSystem::HandleRoomEvent(MediaRoomEvent event)
{
    try {
        if (const auto* signal = std::get_if<MediaSignalTask>(&event)) {
            HandleSignal(*signal);
        }
        else if (const auto* ready = std::get_if<PublisherTrackReadyEvent>(&event)) {
            HandlePublisherTrackReady(*ready);
        }
        else if (const auto* local = std::get_if<LocalMediaSignalEvent>(&event)) {
            HandleLocalMediaSignal(*local);
        }
        else {
            HandleSignalDisconnected(std::get<SignalDisconnectedEvent>(event));
        }
    }
    catch (const std::exception& error) {
        // 库的 SDP 校验和协商接口会抛出标准异常，必须在工作线程入口以内截住。
        // 会话清理由具体操作负责，这里不按事件 UID 猜测失败对象；一个就绪事件可能操作多个成员。
        if (const auto* signal = std::get_if<MediaSignalTask>(&event)) {
            std::cerr << "[media-control-error] type=" << signal->request.signal_type
                      << " meeting=" << signal->request.meeting_id
                      << " uid=" << signal->request.uid
                      << " signal_id=" << signal->request.signal_id;
        }
        else if (const auto* ready = std::get_if<PublisherTrackReadyEvent>(&event)) {
            std::cerr << "[media-control-error] type=track-ready meeting=" << ready->meeting_id
                      << " uid=" << ready->publisher_uid << " instance=" << ready->session_id;
        }
        else if (const auto* local = std::get_if<LocalMediaSignalEvent>(&event)) {
            std::cerr << "[media-control-error] type=local-signal meeting=" << local->response.meeting_id
                      << " uid=" << local->response.uid << " instance=" << local->session_id;
        }
        else {
            std::cerr << "[media-control-error] type=disconnected meeting="
                      << std::get<SignalDisconnectedEvent>(event).meeting_id;
        }
        std::cerr << " error=" << error.what() << std::endl;
    }
}

void LogicSystem::HandleLocalMediaSignal(const LocalMediaSignalEvent& event)
{
    const auto& response = event.response;
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto room_it = m_rooms.find(response.meeting_id);
        if (room_it == m_rooms.end()) {
            return;
        }
        room = room_it->second;
    }

    // 与 Offer、Leave 同队列执行。实例号防止同 UID 重入或同会议号重建后串会话，
    // signal_id 必须仍属于该会话，不能用当前最新编号覆盖旧回调携带的编号。
    const auto peer = room->GetPeer(response.uid);
    const auto media_session = peer ? peer->GetSession() : nullptr;
    if (!media_session || media_session->GetInstanceId() != event.session_id ||
        media_session->GetSignalId() != response.signal_id) {
        return;
    }
    const auto signal_session = event.session.lock();
    if (!signal_session || signal_session->IsStopped() ||
        media_session->GetSignalSession() != signal_session) {
        return;
    }

    if (response.signal_type != "candidate") {
        std::cout << "[media-local-sdp]"
                  << " meeting=" << response.meeting_id
                  << " uid=" << response.uid
                  << " signal_id=" << response.signal_id
                  << " type=" << response.signal_type
                  << " sdp_bytes=" << response.sdp.size()
                  << std::endl;
    }
    Json::Value value;
    value["signal_id"] = static_cast<Json::UInt64>(response.signal_id);
    value["meeting_id"] = static_cast<Json::UInt64>(response.meeting_id);
    value["uid"] = response.uid;
    value["signal_type"] = response.signal_type;
    value["sdp"] = response.sdp;
    value["candidate"] = response.candidate;
    value["mid"] = response.mid;
    // 这里只入 UDS 发送队列。实际写入仍由 Session 的独立发送线程完成。
    signal_session->Send(value.toStyledString());
}

void LogicSystem::HandlePublisherTrackReady(const PublisherTrackReadyEvent& event)
{
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto room_it = m_rooms.find(event.meeting_id);
        if (room_it == m_rooms.end()) {
            return;
        }
        room = room_it->second;
    }

    // 与 Offer、Leave 使用同一 room FIFO。检查与登记之间不会插入该 room 的离会事件。
    // 不能只比较 meeting_id/UID：房间重建或成员重入后，这两个值可能完全相同。
    const auto peer = room->GetPeer(event.publisher_uid);
    const auto session = peer ? peer->GetSession() : nullptr;
    if (!session || session->GetInstanceId() != event.session_id) {
        return;
    }
    const auto connection = session->GetSignalSession();
    if (!connection || connection->IsStopped()) {
        return;
    }
    room->RegisterPublisherTrack(event.publisher_uid, event.media_type);
    RemoveSession(room, nullptr);
}

void LogicSystem::HandleSignal(const MediaSignalTask& task)
{
    // 入队时有效不代表执行时仍有效。Stop 后的旧任务不得创建房间或操作新连接的会话。
    if (!task.session || task.session->IsStopped()) {
        return;
    }
    const MediaSignalRequest& request = task.request;
    if (request.signal_type == "offer") {
        HandleOffer(task.session, request);
        return;
    }
    if (request.signal_type == "answer") {
        HandleAnswer(task.session, request);
        return;
    }
    if (request.signal_type == "candidate") {
        HandleCandidate(task.session, request);
        return;
    }
    if (request.signal_type == "leave") {
        HandleLeave(task.session, request);
    }
}

void LogicSystem::HandleOffer(std::shared_ptr<Session> session, const MediaSignalRequest& request)
{
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        auto room_it = m_rooms.find(request.meeting_id);
        if (room_it == m_rooms.end()) {
            room_it = m_rooms.emplace(request.meeting_id,
                                      std::make_shared<MediaRoom>(request.meeting_id)).first;
        }
        room = room_it->second;
    }

    auto peer = room->GetPeer(request.uid);
    if (peer && peer->GetSession() &&
        (peer->GetSession()->GetSignalId() != request.signal_id ||
         peer->GetSession()->GetSignalSession() != session)) {
        // Realtime 重启后可能重用 signal_id。新连接必须建立新媒体实例，不能继承旧回调。
        room->RemovePeer(request.uid);
        peer.reset();
    }
    if (!peer) {
        peer = std::make_shared<MediaPeer>(request.meeting_id, request.uid);
    }

    auto media_session = peer->GetSession();
    try {
        if (!media_session) {
            const std::weak_ptr<Session> signal_session = session;
            const std::weak_ptr<MediaRoom> media_room = room;
            const std::weak_ptr<LogicSystem> logic_system = weak_from_this();
            const std::uint64_t meeting_id = request.meeting_id;
            media_session = std::make_shared<MediaSession>(
                request.meeting_id,
                request.uid,
                request.signal_id,
                session,
                [logic_system, signal_session](std::uint64_t session_id, MediaSignalResponse response) {
                    if (const auto logic = logic_system.lock()) {
                        const auto meeting_id = response.meeting_id;
                        logic->m_room_workers.Post(meeting_id,
                            LocalMediaSignalEvent{signal_session, session_id, std::move(response)});
                    }
                },
                [logic_system, meeting_id](int publisher_uid, std::uint64_t session_id,
                                           const std::string& media_type) {
                    const auto logic = logic_system.lock();
                    if (!logic) {
                        return;
                    }

                    // 即使通知就在 owner 线程中触发，也只入队，不递归修改 room 或发起协商。
                    // 执行时再检查会话实例，避免 Leave 后排队中的旧通知重新登记发布者。
                    logic->m_room_workers.Post(meeting_id,
                        PublisherTrackReadyEvent{meeting_id, publisher_uid, session_id, media_type});
                },
                [media_room](int publisher_uid, std::uint64_t publisher_session_id,
                             const std::string& media_type, rtc::binary packet) {
                    const auto current_room = media_room.lock();
                    if (!current_room) {
                        return;
                    }

                    // RTP 不经过 RealtimeServer，直接在 MediaServer 的运行时房间内选路和转发。
                    current_room->ForwardRtp(publisher_uid, publisher_session_id, media_type, std::move(packet));
                });
            peer->SetSession(media_session);
            // 完成初始化后再发布到房间，避免媒体回调读取尚在写入的 Peer::m_session。
            room->AddPeer(peer);
        }

        media_session->SetRemoteOffer(request.sdp);
    }
    catch (const std::exception&) {
        // 保留实际操作的对象身份；构造失败时为空，只移除可能留下的空房间。
        RemoveSession(room, media_session);
        throw;
    }
}

void LogicSystem::HandleAnswer(std::shared_ptr<Session> session, const MediaSignalRequest& request)
{
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto room_it = m_rooms.find(request.meeting_id);
        if (room_it == m_rooms.end()) {
            return;
        }
        room = room_it->second;
    }

    const auto peer = room->GetPeer(request.uid);
    if (!peer || !peer->GetSession() || peer->GetSession()->GetSignalId() != request.signal_id ||
        peer->GetSession()->GetSignalSession() != session) {
        return;
    }

    // 客户端 Answer 对应 MediaServer 主动发出的新成员订阅 Offer。
    // 设置后 PeerConnection 回到 Stable，若期间还有新成员加入，会继续生成下一轮 Offer。
    const auto media_session = peer->GetSession();
    try {
        media_session->SetRemoteAnswer(request.sdp);
    }
    catch (const std::exception&) {
        RemoveSession(room, media_session);
        throw;
    }
}

void LogicSystem::HandleCandidate(std::shared_ptr<Session> session, const MediaSignalRequest& request)
{
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto room_it = m_rooms.find(request.meeting_id);
        if (room_it == m_rooms.end()) {
            return;
        }
        room = room_it->second;
    }

    const auto peer = room->GetPeer(request.uid);
    if (!peer || !peer->GetSession() || peer->GetSession()->GetSignalId() != request.signal_id ||
        peer->GetSession()->GetSignalSession() != session) {
        return;
    }
    const auto media_session = peer->GetSession();
    try {
        media_session->AddRemoteCandidate(request.candidate, request.mid);
    }
    catch (const std::exception&) {
        RemoveSession(room, media_session);
        throw;
    }
}

void LogicSystem::HandleLeave(std::shared_ptr<Session> session, const MediaSignalRequest& request)
{
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto room_it = m_rooms.find(request.meeting_id);
        if (room_it == m_rooms.end()) {
            return;
        }
        room = room_it->second;
    }

    const auto peer = room->GetPeer(request.uid);
    if (!peer || !peer->GetSession() || peer->GetSession()->GetSignalId() != request.signal_id ||
        peer->GetSession()->GetSignalSession() != session) {
        return;
    }
    RemoveSession(room, peer->GetSession());
}

void LogicSystem::HandleSignalDisconnected(const SignalDisconnectedEvent& event)
{
    if (!event.session || !event.session->IsStopped()) {
        return;
    }
    std::shared_ptr<MediaRoom> room;
    {
        std::lock_guard<std::mutex> lock(m_rooms_mutex);
        const auto it = m_rooms.find(event.meeting_id);
        if (it == m_rooms.end()) {
            return;
        }
        room = it->second;
    }
    // 与该房间的 Offer 同队列执行。只清理旧连接的成员，重复通知或同会议号重建均安全。
    for (const auto& session : room->GetSessionsForConnection(event.session)) {
        RemoveSession(room, session);
    }
    RemoveSession(room, nullptr);
}

void LogicSystem::RemoveSession(const std::shared_ptr<MediaRoom>& room,
                                const std::shared_ptr<MediaSession>& session)
{
    if (session) {
        // 清理与新 Offer 在同一个 RoomWorker 串行执行；房间内再核对实例号，
        // 即使传入仍被回调保活的旧对象，也不能删除同 UID 的新会话及其消费 Track。
        room->RemovePeer(session->GetUid(), session->GetInstanceId());
    }
    if (room->PeerCount() != 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_rooms_mutex);
    const auto room_it = m_rooms.find(room->GetMeetingId());
    if (room_it != m_rooms.end() && room_it->second == room && room->PeerCount() == 0) {
        m_rooms.erase(room_it);
    }
}
