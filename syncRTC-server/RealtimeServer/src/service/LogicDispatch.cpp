#include "service/LogicSystem.h"
#include "storage/MySqlMgr.h"
#include <json/json.h>
#include <iostream>

namespace {
constexpr std::uint16_t kDisconnected = 0xFFFE;
constexpr std::uint16_t kTimeout = 0xFFFD;
constexpr std::uint16_t kMediaMessage = 0xFFFC;
constexpr std::uint16_t kMediaUp = 0xFFFB;
constexpr std::uint16_t kMediaDown = 0xFFFA;

std::uint16_t ResponseId(std::uint16_t request)
{
    switch (request) {
    case AUTH_LOGIN_REQUEST: return AUTH_LOGIN_RESPONSE;
    case ID_CREATE_MEETING_REQUEST: return ID_CREATE_MEETING_RESPONSE;
    case ID_PAST_MEETING_REQUEST: return ID_PAST_MEETING_RESPONSE;
    case ID_JOIN_MEETING_REQUEST: return ID_JOIN_MEETING_RESPONSE;
    case ID_RESUME_MEETING_REQUEST: return ID_RESUME_MEETING_RESPONSE;
    case ID_START_MEETING_REQUEST: return ID_START_MEETING_RESPONSE;
    case ID_LEAVE_MEETING_REQUEST: return ID_LEAVE_MEETING_RESPONSE;
    case ID_END_MEETING_REQUEST: return ID_END_MEETING_RESPONSE;
    case ID_SEND_MEETING_MESSAGE_REQUEST: return ID_SEND_MEETING_MESSAGE_RESPONSE;
    case ID_GET_MEETING_GROUP_MESSAGES_REQUEST: return ID_GET_MEETING_GROUP_MESSAGES_RESPONSE;
    case ID_GET_MEETING_PRIVATE_MESSAGES_REQUEST: return ID_GET_MEETING_PRIVATE_MESSAGES_RESPONSE;
    case ID_MEDIA_OFFER_REQUEST: return ID_MEDIA_ANSWER_RESPONSE;
    case ID_MEDIA_CANDIDATE_REQUEST: return ID_MEDIA_CANDIDATE_RESPONSE;
    case ID_AI_ASK_REQUEST: return ID_AI_ASK_RESPONSE;
    default: return 0;
    }
}
}

void LogicSystem::ReplyError(const std::shared_ptr<LogicNode>& message, int error)
{
    const auto response = ResponseId(message->id);
    if (!message->session || !response) return;
    Json::Value value;
    value["error"] = error;
    message->session->Send(response, value.toStyledString());
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
    if (!message || !message->session) return;
    const auto session = message->session;
    const bool terminal = message->id == kDisconnected;
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    if (m_stopping) return;
    auto& queue = m_connections[session.get()];
    if (!queue) {
        queue = std::make_shared<SerialCommandQueue>(m_scheduler);
        const std::weak_ptr<SerialCommandQueue> weak = queue;
        queue->on_idle = [this, key = session.get(), weak] {
            std::lock_guard<std::mutex> guard(m_registry_mutex);
            const auto it = m_connections.find(key);
            if (it != m_connections.end() && it->second == weak.lock() && it->second->Pending() == 0)
                m_connections.erase(it);
        };
    }
    if (!queue->Submit([this, message](Operation& op) {
        try { Route(op, message); }
        catch (const std::exception& error) {
            std::cerr << "请求路由失败：" << error.what() << std::endl;
            ReplyError(message, ErrorCodes::ERROR_NETWORK);
        }
    }, {}, terminal, message->message.size())) {
        // 不乱序发送“队列满”响应：关闭连接，并保留随后唯一断线收尾的接纳机会。
        session->RequestClose();
        if (queue->Pending() == 0) m_connections.erase(session.get());
    }
}

bool LogicSystem::SubmitRoom(std::uint64_t id, std::function<void(Operation&, Room&)> command,
                              std::function<void()> done, bool terminal, std::size_t bytes)
{
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    auto& room = m_rooms[id];
    const bool created = !room;
    if (created) {
        room = std::make_shared<Room>(m_scheduler, id);
        const std::weak_ptr<Room> weak = room;
        room->queue->on_idle = [this, id, weak] {
            const auto context = weak.lock();
            if (!context || !context->sessions.empty() || !context->members.empty()) return;
            std::lock_guard<std::mutex> guard(m_registry_mutex);
            const auto it = m_rooms.find(id);
            if (it != m_rooms.end() && it->second == context && context->queue->Pending() == 0)
                m_rooms.erase(it);
        };
    }
    const auto target = room;
    const bool accepted = target->queue->Submit([target, command = std::move(command)](Operation& op) {
        command(op, *target);
    }, std::move(done), terminal, bytes);
    if (!accepted && created) m_rooms.erase(id);
    return accepted;
}

void LogicSystem::Route(Operation& op, const std::shared_ptr<LogicNode>& message)
{
    const auto session = message->session;
    if (!session || (message->id != kDisconnected && session->IsClosed())) return;
    // 在上一条连接命令明确结束之后读取当前归属；不会把入会后的聊天预先路由到普通业务。
    auto meeting = session->GetMeetingId();
    if (message->id == ID_JOIN_MEETING_REQUEST) {
        Json::Value root;
        Json::Reader reader;
        if (meeting || session->GetUserId() <= 0 || !reader.parse(message->message, root) ||
            !root.isObject() || !root["meeting_code"].isString() || root["meeting_code"].asString().empty()) {
            ReplyError(message, meeting ? ErrorCodes::ERROR_MEETING_ACCESS : ErrorCodes::ERROR_JSON);
            return;
        }
        MeetingInfo info;
        const auto code = root["meeting_code"].asString();
        if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoByCode(code, info); })) {
            ReplyError(message, ErrorCodes::ERROR_MEETING_NOT_FOUND);
            return;
        }
        meeting = info.meeting_id;
    } else if (!meeting && message->id != AUTH_LOGIN_REQUEST && message->id != ID_CREATE_MEETING_REQUEST &&
               message->id != ID_PAST_MEETING_REQUEST && message->id != ID_AI_ASK_REQUEST && message->id != kDisconnected) {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(message->message, root) && root.isObject()) {
            try {
                const auto& id = root["meeting_id"];
                if (id.isString()) {
                    std::size_t length = 0;
                    meeting = std::stoull(id.asString(), &length);
                    if (length != id.asString().size()) meeting = 0;
                } else if (id.isUInt64()) meeting = id.asUInt64();
            } catch (const std::exception&) {}
        }
    }
    if (!meeting) {
        Room ordinary(m_scheduler, 0);
        Dispatch(op, ordinary, message);
        return;
    }
    // 连接入口等待会议操作的 done；慢结果直接唤醒会议操作，不进入后续命令尾部。
    op.ReleaseCapacity(); // 只读会议号解析结束，不能占着存储名额等待目标会议再次申请名额。
    bool accepted = false;
    op.Wait([&](std::function<void()> resume) {
        accepted = SubmitRoom(meeting, [this, message](Operation& operation, Room& room) {
            Dispatch(operation, room, message);
        }, resume, message->id == kDisconnected, message->message.size());
        if (!accepted) resume();
    });
    if (!accepted) ReplyError(message, ErrorCodes::ERROR_NETWORK);
}

void LogicSystem::Dispatch(Operation& op, Room& room, const std::shared_ptr<LogicNode>& message)
{
    const auto handler = maps.find(message->id);
    if (handler == maps.end()) return;
    const auto session = message->session;
    if (session && message->id != kDisconnected && session->IsClosed()) return;
    // 旧连接即使已排队，也不能在接管后对新成员执行离会、聊天或媒体操作。
    if (session && room.id && message->id != ID_JOIN_MEETING_REQUEST &&
        message->id != ID_RESUME_MEETING_REQUEST && message->id != kDisconnected) {
        const auto member = room.members.find(session->GetUserId());
        if (member != room.members.end() && member->second.session.lock() != session) {
            ReplyError(message, ErrorCodes::ERROR_MEETING_ACCESS);
            return;
        }
    }
    // 在首个状态变化前预留整条操作的慢任务容量，防止多步写入做到一半因池满退出。
    try {
        if (message->id != ID_AI_ASK_REQUEST && message->id != kMediaMessage)
            op.Reserve(m_storage_pool);
    } catch (const SlowPoolBusy&) { ReplyError(message, ErrorCodes::ERROR_NETWORK); return; }
    // 已入会用户不能借另一个 meeting_id 并发操作另一个房间。
    if (session && room.id && message->id != ID_JOIN_MEETING_REQUEST && message->id != kDisconnected) {
        Json::Value root;
        Json::Reader reader;
        if (reader.parse(message->message, root) && root.isObject() && root.isMember("meeting_id")) {
            try {
                const auto requested = root["meeting_id"].isString()
                    ? std::stoull(root["meeting_id"].asString()) : root["meeting_id"].asUInt64();
                if (requested != room.id) { ReplyError(message, ErrorCodes::ERROR_MEETING_ACCESS); return; }
            } catch (const std::exception&) { ReplyError(message, ErrorCodes::ERROR_JSON); return; }
        }
    }
    try {
    if (message->id == ID_JOIN_MEETING_REQUEST || message->id == ID_RESUME_MEETING_REQUEST) {
        const auto uid = session->GetUserId();
        if (!ReserveUser(uid, room.id)) { ReplyError(message, ErrorCodes::ERROR_MEETING_ACCESS); return; }
        Defer release([this, uid, &room, session] { FinishJoin(uid, room.id, session->GetMeetingId() == room.id); });
        handler->second(op, room, session, message->id, message->message);
    } else {
        handler->second(op, room, session, message->id, message->message);
    }
    } catch (const std::exception& error) {
        // 有响应的 handler 使用 Defer 单次回包，不能在路由层再发送第二份错误。
        std::cerr << "业务操作失败：" << error.what() << std::endl;
    }
}

void LogicSystem::PostSessionDisconnected(std::shared_ptr<Session> session)
{
    if (!session) return;
    auto node = std::make_shared<LogicNode>();
    node->session = std::move(session);
    node->id = kDisconnected;
    PostMsgToQue(std::move(node));
}
void LogicSystem::PostReconnectTimeoutCheck()
{
    // 扫描只允许一个在途任务，存储变慢时不积压每秒一次的重复扫描。
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    if (m_stopping || m_timer_queue->Pending() != 0) return;
    m_timer_queue->Submit([this](Operation& op) {
        Room ordinary(m_scheduler, 0);
        std::uint16_t id = kTimeout;
        std::string message;
        ReconnectTimeoutCheckHandler(op, ordinary, {}, id, message);
    });
}

bool LogicSystem::ReserveUser(int uid, std::uint64_t meeting)
{
    std::lock_guard<std::mutex> lock(m_user_mutex);
    auto [it, inserted] = m_user_meetings.try_emplace(uid, UserPresence{meeting});
    if (!inserted && it->second.meeting != meeting) return false;
    ++it->second.joining;
    return true;
}
void LogicSystem::FinishJoin(int uid, std::uint64_t meeting, bool joined)
{
    std::lock_guard<std::mutex> lock(m_user_mutex);
    const auto it = m_user_meetings.find(uid);
    if (it == m_user_meetings.end() || it->second.meeting != meeting) return;
    --it->second.joining;
    it->second.joined = it->second.joined || joined;
    if (!it->second.joined && it->second.joining == 0) m_user_meetings.erase(it);
}
void LogicSystem::ReleaseUser(int uid, std::uint64_t meeting)
{
    std::lock_guard<std::mutex> lock(m_user_mutex);
    const auto it = m_user_meetings.find(uid);
    if (it != m_user_meetings.end() && it->second.meeting == meeting) m_user_meetings.erase(it);
}
void LogicSystem::ReleaseRoomUsers(std::uint64_t meeting)
{
    std::lock_guard<std::mutex> lock(m_user_mutex);
    for (auto it = m_user_meetings.begin(); it != m_user_meetings.end();)
        if (it->second.meeting == meeting) it = m_user_meetings.erase(it); else ++it;
}

bool LogicSystem::SendMediaSignal(const std::string& message, std::uint64_t epoch)
{
    return m_media_client.Send(epoch, message);
}
std::uint64_t LogicSystem::RegisterSignal(std::uint64_t id, const std::shared_ptr<Session>& session, std::uint64_t meeting)
{
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    const auto epoch = m_media_epoch.load();
    m_signals[id] = {session, meeting, epoch};
    return epoch;
}
LogicSystem::Signal LogicSystem::FindSignal(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    const auto it = m_signals.find(id);
    return it == m_signals.end() ? Signal{} : it->second;
}
void LogicSystem::RemoveSignal(std::uint64_t id)
{
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    m_signals.erase(id);
}
void LogicSystem::ReleaseMediaSession(const std::shared_ptr<Session>& session)
{
    const auto id = session->GetMediaSignalId();
    if (id == 0) return;
    const auto binding = FindSignal(id);
    // 先通知 Media 释放旧 Peer，再删除关联；只能使用该关联的 UDS 代次。
    // UDS 已断开时 Send 会失败，Media 由对应的连接断开事件统一清理。
    if (binding.epoch != 0 && binding.session.lock() == session) {
        Json::Value request;
        request["signal_id"] = static_cast<Json::UInt64>(id);
        request["meeting_id"] = static_cast<Json::UInt64>(binding.meeting);
        request["uid"] = session->GetUserId();
        request["signal_type"] = "leave";
        SendMediaSignal(request.toStyledString(), binding.epoch);
    }
    RemoveSignal(id);
    session->ClearMediaSignalId(id);
}
bool LogicSystem::AcceptMediaEvent(const LogicNode& message)
{
    std::lock_guard<std::mutex> lock(m_signal_mutex);
    const auto current = m_media_epoch.load();
    if (message.id == kMediaMessage) return message.media_epoch == current && m_media_client.IsCurrent(current);
    if (message.media_epoch < current || (message.id == kMediaUp && message.media_epoch == current)) return false;
    for (const auto& [id, signal] : m_signals)
        if (const auto session = signal.session.lock()) session->ClearMediaSignalId(id);
    m_signals.clear();
    m_media_epoch = message.media_epoch;
    return false;
}
void LogicSystem::PostMediaEvent(MediaSignalClient::Event event, std::uint64_t epoch, std::string message)
{
    auto node = std::make_shared<LogicNode>();
    node->id = event == MediaSignalClient::Event::Connected ? kMediaUp :
        event == MediaSignalClient::Event::Disconnected ? kMediaDown : kMediaMessage;
    node->media_epoch = epoch;
    node->message = std::move(message);
    std::lock_guard<std::mutex> lock(m_registry_mutex);
    if (m_stopping) return;
    if (!m_control->Submit([this, node](Operation&) {
        if (!AcceptMediaEvent(*node)) return;
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(node->message, root) || !root.isObject() || !root["signal_id"].isUInt64()) return;
        const auto id = root["signal_id"].asUInt64();
        const auto signal = FindSignal(id);
        if (signal.epoch != node->media_epoch || !signal.meeting) return;
        if (!SubmitRoom(signal.meeting, [this, node, id, signal](Operation& op, Room& room) {
            if (!AcceptMediaEvent(*node)) return;
            const auto current = FindSignal(id);
            if (current.epoch != signal.epoch || current.session.lock() != signal.session.lock()) return;
            MediaSignalResponseHandler(op, room, {}, node->id, node->message);
        }, {}, false, node->message.size())) m_media_client.Invalidate(node->media_epoch);
    }, {}, event != MediaSignalClient::Event::Message, node->message.size())) m_media_client.Invalidate(epoch);
}
