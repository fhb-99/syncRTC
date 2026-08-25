#include "service/LogicSystem.h"

#include "storage/RedisMgr.h"
#include "storage/MySqlMgr.h"
#include "common/data.h"

#include <array>
#include <cerrno>
#include <crypt.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr unsigned long kBcryptCost = 12;
// 被动断线后给客户端的重连宽限时间。到期仍未重新入会，才真正移出会议成员列表。
constexpr int kReconnectGraceSeconds = 45;
// 以下两个 ID 只在服务端内部队列使用，不会通过 TCP 发送给客户端。
// 它们将 I/O 线程发现的断线、timerfd 产生的定时检查统一投递给 LogicSystem 线程。
constexpr std::uint16_t kSessionDisconnectedEventId = 0xFFFE;
constexpr std::uint16_t kReconnectTimeoutCheckEventId = 0xFFFD;
// MediaServer 通过 UDS 返回的信令不会暴露给客户端，先投递到 LogicSystem 查找原 Session。
constexpr std::uint16_t kMediaSignalResponseEventId = 0xFFFC;

std::string HashMeetingPassword(const std::string& password)
{
    char* setting = crypt_gensalt_ra("$2b$", kBcryptCost, nullptr, 0);
    if (!setting) {
        return {};
    }

    void* data = nullptr;
    int data_size = 0;
    char* hash = crypt_ra(password.c_str(), setting, &data, &data_size);
    std::string result = hash ? hash : "";

    std::free(setting);
    std::free(data);
    return result;
}

bool CheckMeetingPassword(const std::string& password, const std::string& password_hash)
{
    if (password.empty() || password_hash.empty()) {
        return false;
    }

    void* data = nullptr;
    int data_size = 0;
    char* hash = crypt_ra(password.c_str(), password_hash.c_str(), &data, &data_size);
    const std::string result = hash ? hash : "";

    std::free(data);
    return !result.empty() && result == password_hash;
}

std::string RoomMembersKey(std::uint64_t meeting_id)
{
    return "room:" + std::to_string(meeting_id) + ":members";
}

std::string RoomMemberStateKey(std::uint64_t meeting_id, int uid)
{
    return "room:" + std::to_string(meeting_id) + ":member:" + std::to_string(uid);
}

std::string ReconnectDeadlineKey()
{
    // 所有“等待重连”的成员共用一个有序集合：score 是超时秒时间戳。
    // 定时检查只查询已到期成员，避免逐个扫描所有会议和所有成员。
    return "room:reconnect_deadlines";
}

std::string ReconnectDeadlineMember(std::uint64_t meeting_id, int uid)
{
    // member 使用 meeting_id:uid，既能在同一个有序集合中唯一定位成员，
    // 也能在超时检查时反解析出所属会议和用户。
    return std::to_string(meeting_id) + ":" + std::to_string(uid);
}

std::string MeetingStatusText(MeetingStatus status)
{
    switch (status) {
    case MeetingStatus::kInProgress:
        return "in_progress";
    case MeetingStatus::kEnded:
        return "ended";
    case MeetingStatus::kScheduled:
    default:
        return "scheduled";
    }
}

bool ReadUInt64Value(const Json::Value& value, std::uint64_t& result)
{
    if (value.isUInt64() || value.isUInt() || value.isInt64() || value.isInt()) {
        if ((value.isInt() && value.asInt() < 0) ||
            (value.isInt64() && value.asInt64() < 0)) {
            return false;
        }
        result = value.asUInt64();
        return true;
    }

    if (!value.isString()) {
        return false;
    }

    try {
        const std::string text = value.asString();
        std::size_t parsed_length = 0;
        result = std::stoull(text, &parsed_length);
        return parsed_length == text.size();
    }
    catch (const std::exception&) {
        return false;
    }
}

Json::Value MeetingMessageToJson(const MeetingMessageInfo& message,
                                 const std::string& sender_name,
                                 bool is_mine)
{
    Json::Value value;
    value["message_id"] = std::to_string(message.message_id);
    value["client_msg_id"] = message.client_msg_id;
    value["meeting_id"] = std::to_string(message.meeting_id);
    value["chat_type"] = message.receiver_user_id.has_value() ? "private" : "group";
    value["sender_user_id"] = message.sender_user_id;
    value["sender_name"] = sender_name;
    value["receiver_user_id"] = message.receiver_user_id.has_value()
        ? Json::Value(static_cast<Json::UInt64>(message.receiver_user_id.value()))
        : Json::Value(Json::nullValue);
    value["content"] = message.content;
    value["created_at"] = message.created_at;
    value["is_mine"] = is_mine;
    return value;
}

} // namespace

LogicSystem::LogicSystem()
    : m_stop(false)
{
    initHandlers();
    work_thread = std::thread(&LogicSystem::DealMessage, this);
}

LogicSystem::~LogicSystem()
{
    if (m_media_fd != -1) {
        ::shutdown(m_media_fd, SHUT_RDWR);
    }
    if (m_media_read_thread.joinable()) {
        m_media_read_thread.join();
    }
    if (m_media_fd != -1) {
        ::close(m_media_fd);
        m_media_fd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cond.notify_all();

    if (work_thread.joinable()) {
        work_thread.join();
    }
}

void LogicSystem::StartMediaSignalClient(const std::string& socket_path)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("MediaServer UDS 地址过长");
    }
    std::memcpy(address.sun_path, socket_path.data(), socket_path.size());

    m_media_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_media_fd == -1) {
        throw std::runtime_error("创建 MediaServer UDS 连接失败：" +
                                 std::string(std::strerror(errno)));
    }
    if (::connect(m_media_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        const std::string error = std::strerror(errno);
        ::close(m_media_fd);
        m_media_fd = -1;
        throw std::runtime_error("连接 MediaServer UDS 失败：" + error);
    }

    // UDS 读线程只负责收完整内部帧，客户端 TCP 写回仍统一由 LogicSystem 线程处理。
    m_media_read_thread = std::thread(&LogicSystem::ReadMediaSignalResponses, this);
}

bool LogicSystem::SendMediaSignal(const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_media_send_mutex);
    if (m_media_fd == -1) {
        return false;
    }

    const auto body_length = static_cast<std::uint32_t>(message.size());
    std::string frame(sizeof(std::uint32_t) + message.size(), '\0');
    frame[0] = static_cast<char>((body_length >> 24U) & 0xFFU);
    frame[1] = static_cast<char>((body_length >> 16U) & 0xFFU);
    frame[2] = static_cast<char>((body_length >> 8U) & 0xFFU);
    frame[3] = static_cast<char>(body_length & 0xFFU);
    frame.replace(sizeof(std::uint32_t), message.size(), message);

    std::size_t sent_length = 0;
    while (sent_length < frame.size()) {
        const ssize_t sent = ::send(m_media_fd, frame.data() + sent_length,
                                    frame.size() - sent_length, MSG_NOSIGNAL);
        if (sent > 0) {
            sent_length += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == -1 && errno == EINTR) {
            continue;
        }
        return false;
    }

    return true;
}

void LogicSystem::ReadMediaSignalResponses()
{
    constexpr std::size_t kFrameHeaderLength = sizeof(std::uint32_t);
    std::array<char, 4096> read_buffer{};
    std::string receive_buffer;

    while (true) {
        const ssize_t read_length = ::recv(m_media_fd, read_buffer.data(), read_buffer.size(), 0);
        if (read_length > 0) {
            receive_buffer.append(read_buffer.data(), static_cast<std::size_t>(read_length));

            while (receive_buffer.size() >= kFrameHeaderLength) {
                const auto* header = reinterpret_cast<const unsigned char*>(receive_buffer.data());
                const std::uint32_t body_length =
                    (static_cast<std::uint32_t>(header[0]) << 24U) |
                    (static_cast<std::uint32_t>(header[1]) << 16U) |
                    (static_cast<std::uint32_t>(header[2]) << 8U) |
                    static_cast<std::uint32_t>(header[3]);
                const std::size_t frame_length = kFrameHeaderLength + body_length;
                if (receive_buffer.size() < frame_length) {
                    break;
                }

                PostMediaSignalResponse(
                    receive_buffer.substr(kFrameHeaderLength, body_length));
                receive_buffer.erase(0, frame_length);
            }
            continue;
        }
        if (read_length == -1 && errno == EINTR) {
            continue;
        }
        return;
    }
}

void LogicSystem::PostMediaSignalResponse(std::string message)
{
    auto response = std::make_shared<LogicNode>();
    response->id = kMediaSignalResponseEventId;
    response->message = std::move(message);
    PostMsgToQue(std::move(response));
}

void LogicSystem::PostMsgToQue(std::shared_ptr<LogicNode> message)
{
    if (!message) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_msg_que.push(std::move(message));
    }
    m_cond.notify_one();
}

void LogicSystem::PostSessionDisconnected(std::shared_ptr<Session> session)
{
    if (!session) {
        return;
    }

    // CServer 的 I/O 线程只负责发现并关闭 TCP 连接；成员列表和 Redis 只能由
    // LogicSystem 线程串行修改，避免它与入会、主动离会请求并发写同一会议状态。
    // 传递 Session 指针而不是 fd，是因为 fd 在 close 后可能被系统复用；Session
    // 对象能准确表示本次已经关闭的连接。
    auto message = std::make_shared<LogicNode>();
    message->session = std::move(session);
    message->id = kSessionDisconnectedEventId;
    PostMsgToQue(std::move(message));
}

void LogicSystem::PostReconnectTimeoutCheck()
{
    // timerfd 线程只投递检查任务。Redis 查询、成员删除和广播都放在 LogicSystem
    // 线程执行，避免 epoll 线程被业务逻辑或存储访问阻塞。
    // 该事件没有对应的客户端 Session，因为它表示检查所有已过期的重连成员。
    auto message = std::make_shared<LogicNode>();
    message->id = kReconnectTimeoutCheckEventId;
    PostMsgToQue(std::move(message));
}

void LogicSystem::DealMessage()
{
    while (true) {
        std::shared_ptr<LogicNode> message;
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

        const auto handler = maps.find(message->id);
        if (handler == maps.end()) {
            std::cerr << "未注册的业务消息，id=" << message->id << std::endl;
            continue;
        }

        // 具体业务回调后续在 initHandlers 中注册，回调可通过 session->Send 写入响应。
        handler->second(message->session, message->id, std::move(message->message));
    }
}

void LogicSystem::initHandlers()
{
    // 当前只搭建投递和回调框架，认证、会议等业务处理后续再在此注册
    maps[AUTH_LOGIN_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message){
        LoginHandler(session, id, message);
    };

    // 处理客户端的创建会议请求
    maps[ID_CREATE_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        CreateMeetingHandler(session, id, message);
    };

    // 处理客户端的得到历史会议请求
    maps[ID_PAST_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message){
        GetPastMeetingHandler(session, id, message);
    };

    // 处理入会
    maps[ID_JOIN_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        JoinMeeetingHandler(session, id, message);
    };

    maps[ID_START_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        StartMeetingHandler(session, id, message);
    };

    maps[ID_LEAVE_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        LeaveMeetingHandler(session, id, message);
    };

    maps[ID_END_MEETING_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        EndMeetingHandler(session, id, message);
    };

    maps[ID_SEND_MEETING_MESSAGE_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        SendMeetingMessageHandler(session, id, message);
    };

    maps[ID_GET_MEETING_GROUP_MESSAGES_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        GetMeetingGroupMessagesHandler(session, id, message);
    };

    maps[ID_GET_MEETING_PRIVATE_MESSAGES_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        GetMeetingPrivateMessagesHandler(session, id, message);
    };

    maps[ID_MEDIA_OFFER_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaOfferHandler(session, id, message);
    };

    maps[ID_MEDIA_CANDIDATE_REQUEST] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaCandidateHandler(session, id, message);
    };

    maps[kMediaSignalResponseEventId] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaSignalResponseHandler(session, id, message);
    };

    maps[kSessionDisconnectedEventId] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        SessionDisconnectedHandler(session, id, message);
    };

    maps[kReconnectTimeoutCheckEventId] = [this](std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        ReconnectTimeoutCheckHandler(session, id, message);
    };
}


void LogicSystem::LoginHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(message, root);
    auto uid = root["uid"].asInt();
    auto token = root["token"].asString();
    auto email = root["email"].asString();
    std::cout << "uid is: " << uid << std::endl;

    Json::Value res;
    Defer defer([this, session, &res](){
        std::string str = res.toStyledString();
        session->Send(AUTH_LOGIN_RESPONSE, str);
    });

    // 从redis当中取出token
    std::string tmp;
    bool is_id = RedisMgr::GetInstance()->Get("auth::session:" + token, tmp);
    if(!is_id) {
        res["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    Json::Value session_info;
    Json::Reader session_reader;
    if (!session_reader.parse(tmp, session_info)) {
        res["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    const int user_id = session_info["uid"].asInt();
    if (user_id <= 0 || uid != user_id) {
        res["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    UserInfo user_info;
    bool is_user = MysqlMgr::GetInstance()->GetUserInfo(email, user_info);
    if(!is_user || user_info.uid <= 0 || user_info.username.empty()) {
        std::cout << "Get user info failed" << std::endl;
		res["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    res["email"] = user_info.email;
    res["username"] = user_info.username;

    // 后续还要查询会议参与表，来返回给客户端展示界面的数据
    std::vector<RecentMeetingInfo> meetings;
    bool is_recent = MysqlMgr::GetInstance()->GetMeetingRecently(user_id, meetings);
    if(!is_recent) {
        res["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    // 将会议列表逐条转换为 JSON，便于 Qt 客户端直接解析。
    Json::Value meeting_list(Json::arrayValue);
    for (const auto& meeting : meetings) {
        Json::Value meeting_json;
        meeting_json["meeting_code"] = meeting.meeting_code;
        meeting_json["title"] = meeting.title;
        meeting_json["creator_display_name"] = meeting.creator_display_name;
        meeting_json["creator_avatar_url"] = meeting.creator_avatar_url;
        meeting_json["status"] = static_cast<Json::UInt>(meeting.status);
        meeting_json["requires_password"] = meeting.requires_password;
        meeting_json["max_participants"] = meeting.max_participants;
        meeting_json["scheduled_at"] = meeting.scheduled_at;
        meeting_list.append(std::move(meeting_json));
    }
    res["meetings"] = std::move(meeting_list);
    // Redis 校验和用户资料查询均成功后，才将身份绑定到当前长连接。
    session->SetUserId(user_id);
    res["error"] = ErrorCodes::SUCCESS;
}



void LogicSystem::CreateMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_CREATE_MEETING_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["title"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string title = root["title"].asString();
    if (title.empty() || title.size() > 200) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::string scheduled_at;
    if (!root["scheduled_at"].isNull()) {
        if (!root["scheduled_at"].isString()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        scheduled_at = root["scheduled_at"].asString();
    }

    std::string password;
    if (!root["password"].isNull()) {
        if (!root["password"].isString()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        password = root["password"].asString();
    }
    // bcrypt 只处理前 72 字节，拒绝超长密码以免服务端静默截断。
    if (password.size() > 72) {
        value["error"] = ErrorCodes::ERROR_PASSWORD;
        return;
    }

    const int user_id = session->GetUserId();
    if (user_id <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    CreateMeetingInfo create_info;
    create_info.user_id = user_id;
    create_info.title = title;
    create_info.scheduled_at = scheduled_at;
    if (!password.empty()) {
        create_info.password_hash = HashMeetingPassword(password);
        if (create_info.password_hash.empty()) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }
    }

    RecentMeetingInfo meeting;
    if (!MysqlMgr::GetInstance()->CreateMeeting(create_info, meeting)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }
    // 列表顺序和数量由 MySQL 统一决定，客户端直接用返回结果替换当前列表。
    std::vector<RecentMeetingInfo> meetings;
    if (!MysqlMgr::GetInstance()->GetMeetingRecently(user_id, meetings)) {
        // 创建事务已提交，不能把创建结果误报为失败。
        value["meetings_sync"] = false;
        value["error"] = ErrorCodes::SUCCESS;
        return;
    }

    Json::Value meeting_list(Json::arrayValue);
    for (const auto& meeting : meetings) {
        Json::Value meeting_json;
        meeting_json["meeting_code"] = meeting.meeting_code;
        meeting_json["title"] = meeting.title;
        meeting_json["creator_display_name"] = meeting.creator_display_name;
        meeting_json["creator_avatar_url"] = meeting.creator_avatar_url;
        meeting_json["status"] = static_cast<Json::UInt>(meeting.status);
        meeting_json["requires_password"] = meeting.requires_password;
        meeting_json["max_participants"] = meeting.max_participants;
        meeting_json["scheduled_at"] = meeting.scheduled_at;
        meeting_list.append(std::move(meeting_json));
    }
    value["meetings"] = std::move(meeting_list);
    value["meetings_sync"] = true;
    value["error"] = ErrorCodes::SUCCESS;
}




void LogicSystem::GetPastMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string&)
{
    if (!session) {
        return;
    }


    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_PAST_MEETING_RESPONSE, value.toStyledString());
    });

    const int user_id = session->GetUserId();
    if (user_id <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    std::vector<HistoryMeetingInfo> meetings;
    if (!MysqlMgr::GetInstance()->GetHistoryMeeting(user_id, meetings)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value meeting_list(Json::arrayValue);
    for(const auto& meeting : meetings) {
        Json::Value meeting_json;
        meeting_json["meeting_code"] = meeting.meeting_code;
        meeting_json["title"] = meeting.title;
        meeting_json["creator_display_name"] = meeting.creator_display_name;
        meeting_json["creator_avatar_url"] = meeting.creator_avatar_url;
        meeting_json["started_at"] = meeting.started_at;
        meeting_json["ended_at"] = meeting.ended_at;
        meeting_list.append(std::move(meeting_json));
    }

    value["meetings"] = std::move(meeting_list);
    value["error"] = ErrorCodes::SUCCESS;
}



void LogicSystem::JoinMeeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_JOIN_MEETING_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["meeting_code"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_code = root["meeting_code"].asString();
    if(meeting_code.empty()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if(uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    MeetingInfo meeting_info;
    if(!MysqlMgr::GetInstance()->GetMeetingInfoByCode(meeting_code, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }

    // 加入会议只进入房间，不改变会议生命周期；已取消或已结束的会议都不能重新入会。
    if (meeting_info.status == MeetingStatus::kCancelled ||
        meeting_info.status == MeetingStatus::kEnded) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    if (meeting_info.requires_password) {
        if (!root["password"].isString()) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }

        std::string password_hash;
        if (!MysqlMgr::GetInstance()->GetMeetingPasswordHash(meeting_info.meeting_id,
                                                             password_hash) ||
            !CheckMeetingPassword(root["password"].asString(), password_hash)) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }
    }

    const std::string uid_str = std::to_string(uid);
    const std::string room_key = RoomMembersKey(meeting_info.meeting_id);
    const std::string member_state_key = RoomMemberStateKey(meeting_info.meeting_id, uid);
    const std::string deadline_member = ReconnectDeadlineMember(meeting_info.meeting_id, uid);
    auto redis = RedisMgr::GetInstance();
    const std::string role =
        meeting_info.creator_user_id == static_cast<std::uint64_t>(uid) ? "host" : "participant";


    // Redis 保存当前正在房间里的成员；它不是历史记录，只服务实时通信。
    const bool already_in_room = redis->SIsMember(room_key, uid_str);
    const int current_count = redis->SCard(room_key);
    if (!already_in_room && current_count >= meeting_info.max_participants) {
        value["error"] = ErrorCodes::ERROR_MEETING_FULL;
        return;
    }

    bool is_reconnected = false;
    if (already_in_room) {
        if (redis->HGet(member_state_key, "room_state") != "reconnecting") {
            // 同一用户在会议中已有活跃连接，不允许第二台设备同时参会。
            value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
            return;
        }

        try {
            const std::time_t deadline = std::stoll(redis->HGet(member_state_key, "reconnect_deadline_at"));
            if (deadline <= std::time(nullptr)) {
                value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
                return;
            }
        }
        catch (const std::exception&) {
            value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
            return;
        }
        is_reconnected = true;
    }

    if(!redis->SAdd(room_key, uid_str)) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    // MySQL 保存“这个用户参加过这场会议”的事实，后续历史会议就靠它查询。
    if (!MysqlMgr::GetInstance()->UpdateMeetingPart(meeting_info, uid)) {
        if (!already_in_room) {
            redis->SRem(room_key, uid_str);
        }
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    const std::string presence_key = "presence:" + uid_str;
    if (!redis->HSet(presence_key, "status", "in_meeting") ||
        !redis->HSet(presence_key, "meeting_id", std::to_string(meeting_info.meeting_id))) {
        if (!already_in_room) {
            redis->SRem(room_key, uid_str);
        }
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    if (!already_in_room && !redis->HSet(member_state_key, "room_state", "active")) {
        redis->SRem(room_key, uid_str);
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }
    if (is_reconnected &&
        (!redis->HSet(member_state_key, "room_state", "active") ||
         !redis->HSet(member_state_key, "reconnect_deadline_at", "") ||
         !redis->ZRem(ReconnectDeadlineKey(), deadline_member))) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    std::vector<std::string> members;
    if (!redis->SMembers(room_key, members)) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    // 入会成功后再绑定会议 ID，避免失败请求污染当前连接状态。
    session->SetMeetingId(meeting_info.meeting_id);
    auto& meeting_sessions = m_meeting_sessions[meeting_info.meeting_id];
    meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
        [&session, uid, is_reconnected](const std::weak_ptr<Session>& item) {
            const auto active_session = item.lock();
            return !active_session || active_session == session ||
                   (is_reconnected && active_session->GetUserId() == uid);
        }), meeting_sessions.end());
    meeting_sessions.push_back(session);

    Json::Value member_array(Json::arrayValue);
    Json::Value joined_member;
    for (const auto& member : members) {
        try {
            const std::uint64_t member_uid = std::stoull(member);
            Json::Value member_value;
            member_value["user_id"] = static_cast<Json::UInt64>(member_uid);
            member_value["is_self"] = member_uid == static_cast<std::uint64_t>(uid);
            const std::string room_state = redis->HGet(
                RoomMemberStateKey(meeting_info.meeting_id, static_cast<int>(member_uid)), "room_state");
            member_value["room_state"] = room_state.empty() ? "active" : room_state;

            std::string member_name;
            if (MysqlMgr::GetInstance()->GetUserDisplayNameById(
                    static_cast<int>(member_uid), member_name)) {
                member_value["name"] = member_name;
            } else {
                member_value["name"] = "Member " + member;
            }
            if (member_uid == static_cast<std::uint64_t>(uid)) {
                joined_member = member_value;
            }
            member_array.append(std::move(member_value));
        }
        catch (const std::exception&) {
            // 理论上成员都是 uid；这里兜底避免脏数据导致响应构造失败。
            Json::Value member_value;
            member_value["user_id"] = member;
            member_value["name"] = "Member " + member;
            member_value["is_self"] = false;
            member_value["room_state"] = "active";
            if (member == uid_str) {
                joined_member = member_value;
            }
            member_array.append(std::move(member_value));
        }
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_info.meeting_id);
    value["meeting_code"] = meeting_info.meeting_code;
    value["status"] = MeetingStatusText(meeting_info.status);
    value["role"] = role;
    value["members"] = member_array;
    value["title"] = meeting_info.title;
    value["creator_user_id"] = static_cast<Json::UInt64>(meeting_info.creator_user_id);
    value["max_participants"] = meeting_info.max_participants;
    value["current_participants"] = static_cast<Json::UInt>(members.size());
    value["member_uids"] = member_array;

    // Redis 新增成员后才广播。重复入会或同账号多设备进入不会被误认为新人。
    if (!already_in_room && joined_member.isObject()) {
        Json::Value notification;
        notification["meeting_id"] = std::to_string(meeting_info.meeting_id);
        // is_self 仅对入会回包有意义；其他用户收到的新人永远不是自己。
        joined_member["is_self"] = false;
        notification["member"] = joined_member;
        notification["current_participants"] = static_cast<Json::UInt>(members.size());
        const std::string notification_text = notification.toStyledString();

        for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
            const auto meeting_session = it->lock();
            if (!meeting_session) {
                it = meeting_sessions.erase(it);
                continue;
            }

            // 新加入用户会通过 JOIN_MEETING 回包得到完整成员列表，广播只通知其他用户。
            if (meeting_session->GetUserId() != uid) {
                meeting_session->Send(ID_MEETING_MEMBER_JOINED, notification_text);
            }
            ++it;
        }
    }
    if (is_reconnected) {
        Json::Value notification;
        notification["meeting_id"] = std::to_string(meeting_info.meeting_id);
        notification["user_id"] = uid;
        notification["room_state"] = "active";
        const std::string notification_text = notification.toStyledString();

        for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
            const auto meeting_session = it->lock();
            if (!meeting_session) {
                it = meeting_sessions.erase(it);
                continue;
            }
            if (meeting_session->GetUserId() != uid) {
                meeting_session->Send(ID_MEETING_MEMBER_RECONNECTED, notification_text);
            }
            ++it;
        }
    }

}

void LogicSystem::StartMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_START_MEETING_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() || !root["meeting_id"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_id_text = root["meeting_id"].asString();
    std::uint64_t meeting_id = 0;
    try {
        std::size_t parsed_length = 0;
        meeting_id = std::stoull(meeting_id_text, &parsed_length);
        if (meeting_id == 0 || parsed_length != meeting_id_text.size()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    }
    catch (const std::exception&) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    MeetingInfo meeting_info;
    if (!MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }

    // 当前表结构未保存 co_host，先以创建者作为唯一可开始会议的主持人。
    if (meeting_info.creator_user_id != static_cast<std::uint64_t>(uid)) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }
    if (meeting_info.status != MeetingStatus::kScheduled) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }
    if (!MysqlMgr::GetInstance()->StartMeeting(meeting_id, uid)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_id);
    value["status"] = "in_progress";

    Json::Value notification;
    notification["meeting_id"] = std::to_string(meeting_id);
    notification["status"] = "in_progress";
    const std::string notification_text = notification.toStyledString();

    const auto sessions_it = m_meeting_sessions.find(meeting_id);
    if (sessions_it == m_meeting_sessions.end()) {
        return;
    }

    auto& meeting_sessions = sessions_it->second;
    for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
        const auto meeting_session = it->lock();
        if (!meeting_session) {
            it = meeting_sessions.erase(it);
            continue;
        }

        meeting_session->Send(ID_MEETING_STARTED, notification_text);
        ++it;
    }
}

void LogicSystem::SessionDisconnectedHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string&)
{
    // 被动断线处理只改变在线房间状态，不会结束会议，也不会立即将用户从 Redis
    // 的会议成员集合删除。用户仍可在 kReconnectGraceSeconds 秒内重新入会。
    if (!session) {
        return;
    }

    const std::uint64_t meeting_id = session->GetMeetingId();
    if (meeting_id == 0) {
        return;
    }

    const int uid = session->GetUserId();
    const auto sessions_it = m_meeting_sessions.find(meeting_id);
    if (sessions_it == m_meeting_sessions.end()) {
        session->SetMeetingId(0);
        return;
    }

    auto& meeting_sessions = sessions_it->second;
    // 同一 uid 可能已经通过另一台设备重新入会。旧连接延迟触发关闭事件时，
    // 不能把新连接误标记为掉线，因此需要同时确认旧 Session 是否还在列表中，
    // 以及列表中是否仍存在相同 uid 的另一条有效连接。
    bool contains_disconnected_session = false;
    bool has_other_connection = false;
    for (const auto& item : meeting_sessions) {
        const auto active_session = item.lock();
        if (!active_session) {
            continue;
        }
        if (active_session == session) {
            contains_disconnected_session = true;
        } else if (active_session->GetUserId() == uid) {
            has_other_connection = true;
        }
    }

    // 只移除本次关闭的本地投递通道。m_meeting_sessions 仅用于本进程广播，
    // 不代表 Redis 中的会议成员资格。
    meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
        [&session](const std::weak_ptr<Session>& item) {
            const auto active_session = item.lock();
            return !active_session || active_session == session;
        }), meeting_sessions.end());

    std::string notification_text;
    if (contains_disconnected_session && !has_other_connection && uid > 0) {
        const std::string uid_text = std::to_string(uid);
        const std::string room_key = RoomMembersKey(meeting_id);
        const std::string member_state_key = RoomMemberStateKey(meeting_id, uid);
        const std::string deadline_member = ReconnectDeadlineMember(meeting_id, uid);
        const auto redis = RedisMgr::GetInstance();

        // 被动断线时保留 room:{meeting_id}:members 中的 uid，只写入三类重连信息：
        // 1. member hash 的 room_state=reconnecting，供客户端和重连请求识别；
        // 2. member hash 的 reconnect_deadline_at，供重连时判断是否仍在宽限期；
        // 3. 全局 ZSet 的截止记录，供定时器高效筛选超时成员。
        // 三项均成功后才构造广播，防止客户端已看到“重连中”而服务端没有超时依据。
        if (redis->SIsMember(room_key, uid_text) &&
            redis->HGet(member_state_key, "room_state") != "reconnecting") {
            const std::time_t deadline = std::time(nullptr) + kReconnectGraceSeconds;
            if (redis->HSet(member_state_key, "room_state", "reconnecting") &&
                redis->HSet(member_state_key, "reconnect_deadline_at", std::to_string(deadline)) &&
                redis->ZAdd(ReconnectDeadlineKey(), deadline_member, static_cast<double>(deadline))) {
                Json::Value notification;
                notification["meeting_id"] = std::to_string(meeting_id);
                notification["user_id"] = uid;
                notification["room_state"] = "reconnecting";
                notification["reconnect_deadline_at"] = static_cast<Json::Int64>(deadline);
                notification["current_participants"] = redis->SCard(room_key);
                notification_text = notification.toStyledString();
            } else {
                // 写入未完整成功时恢复 active，并清除半成品截止记录；否则成员可能
                // 永久卡在 reconnecting，或被定时器错误移出会议。
                redis->HSet(member_state_key, "room_state", "active");
                redis->HSet(member_state_key, "reconnect_deadline_at", "");
                redis->ZRem(ReconnectDeadlineKey(), deadline_member);
                std::cerr << "更新会议成员重连状态失败，meeting_id=" << meeting_id
                          << ", uid=" << uid << std::endl;
            }
        }
    }

    // 断线用户本身已无可用 TCP 连接；只向当前进程内同会议的其他在线用户广播。
    // 相同 uid 的新连接不接收旧连接的断线通知，避免其界面回退为 reconnecting。
    if (!notification_text.empty()) {
        for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
            const auto meeting_session = it->lock();
            if (!meeting_session) {
                it = meeting_sessions.erase(it);
                continue;
            }

            if (meeting_session->GetUserId() != uid) {
                meeting_session->Send(ID_MEETING_MEMBER_RECONNECTING, notification_text);
            }
            ++it;
        }
    }

    if (meeting_sessions.empty()) {
        m_meeting_sessions.erase(sessions_it);
    }

    // 清除旧 Session 的会议上下文。若关闭事件被重复投递，meeting_id 为 0 会使
    // 本函数直接返回，从而保证对同一条断线连接的处理幂等。
    session->SetMeetingId(0);
}

void LogicSystem::ReconnectTimeoutCheckHandler(std::shared_ptr<Session>, std::uint16_t&, std::string&)
{
    // timerfd 周期性触发该处理。先从全局 ZSet 查询 score <= 当前时间的成员，
    // 无需遍历全部会议；检查成本只与本次实际到期的重连成员有关。
    const std::time_t now = std::time(nullptr);
    const auto redis = RedisMgr::GetInstance();
    std::vector<std::string> expired_members;
    if (!redis->ZRangeByScore(ReconnectDeadlineKey(), static_cast<double>(now), expired_members)) {
        return;
    }

    for (const std::string& deadline_member : expired_members) {
        // ZSet member 必须是 meeting_id:uid。格式异常的数据无法定位具体成员，
        // 直接删除索引，避免它在之后的每次定时检查中重复出现。
        const std::size_t separator = deadline_member.find(':');
        if (separator == std::string::npos) {
            redis->ZRem(ReconnectDeadlineKey(), deadline_member);
            continue;
        }

        std::uint64_t meeting_id = 0;
        int uid = 0;
        try {
            meeting_id = std::stoull(deadline_member.substr(0, separator));
            uid = std::stoi(deadline_member.substr(separator + 1));
        }
        catch (const std::exception&) {
            redis->ZRem(ReconnectDeadlineKey(), deadline_member);
            continue;
        }

        const std::string member_state_key = RoomMemberStateKey(meeting_id, uid);
        const std::string deadline_text = redis->HGet(member_state_key, "reconnect_deadline_at");
        std::time_t deadline = 0;
        try {
            deadline = std::stoll(deadline_text);
        }
        catch (const std::exception&) {
            redis->ZRem(ReconnectDeadlineKey(), deadline_member);
            continue;
        }

        if (redis->HGet(member_state_key, "room_state") != "reconnecting" || deadline > now) {
            // 用户已经重连，或重连期间生成了新的截止时间。这条到期索引已失效，
            // 仅清理索引，不能删除当前仍有效的会议成员资格。
            redis->ZRem(ReconnectDeadlineKey(), deadline_member);
            continue;
        }

        const std::string uid_text = std::to_string(uid);
        const std::string room_key = RoomMembersKey(meeting_id);
        // 宽限期真正到期后才移除成员：删除房间集合、超时索引和成员 hash，并将
        // presence 恢复为普通 online。这里不修改 meetings 表或会议生命周期，
        // 会议是否结束仍只能由主持人发起结束会议请求。
        if (!redis->SRem(room_key, uid_text) || !redis->ZRem(ReconnectDeadlineKey(), deadline_member)) {
            std::cerr << "清理重连超时成员失败，meeting_id=" << meeting_id
                      << ", uid=" << uid << std::endl;
            continue;
        }

        redis->Del(member_state_key);
        redis->HSet("presence:" + uid_text, "status", "online");
        redis->HSet("presence:" + uid_text, "meeting_id", "");

        Json::Value notification;
        notification["meeting_id"] = std::to_string(meeting_id);
        notification["user_id"] = uid;
        notification["current_participants"] = redis->SCard(room_key);
        const std::string notification_text = notification.toStyledString();

        // timeout 用户在断线时已经从本地连接列表移除，因此仅向同会议的现存连接
        // 广播 MEMBER_TIMEOUT_LEFT，让客户端删除该成员并刷新成员列表。
        const auto sessions_it = m_meeting_sessions.find(meeting_id);
        if (sessions_it == m_meeting_sessions.end()) {
            continue;
        }

        auto& meeting_sessions = sessions_it->second;
        for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
            const auto meeting_session = it->lock();
            if (!meeting_session) {
                it = meeting_sessions.erase(it);
                continue;
            }
            meeting_session->Send(ID_MEETING_MEMBER_TIMEOUT_LEFT, notification_text);
            ++it;
        }
        if (meeting_sessions.empty()) {
            m_meeting_sessions.erase(sessions_it);
        }
    }
}

void LogicSystem::LeaveMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_LEAVE_MEETING_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() || !root["meeting_id"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_id_text = root["meeting_id"].asString();
    std::uint64_t meeting_id = 0;
    try {
        std::size_t parsed_length = 0;
        meeting_id = std::stoull(meeting_id_text, &parsed_length);
        if (meeting_id == 0 || parsed_length != meeting_id_text.size()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    }
    catch (const std::exception&) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    const std::string uid_text = std::to_string(uid);
    const std::string room_key = RoomMembersKey(meeting_id);
    auto redis = RedisMgr::GetInstance();
    if (!redis->SRem(room_key, uid_text) ||
        !redis->HSet("presence:" + uid_text, "status", "online") ||
        !redis->HSet("presence:" + uid_text, "meeting_id", "")) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    const auto sessions_it = m_meeting_sessions.find(meeting_id);
    if (sessions_it != m_meeting_sessions.end()) {
        auto& meeting_sessions = sessions_it->second;
        meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
            [&session](const std::weak_ptr<Session>& item) {
                const auto active_session = item.lock();
                return !active_session || active_session == session;
            }), meeting_sessions.end());

        Json::Value notification;
        notification["meeting_id"] = std::to_string(meeting_id);
        notification["user_id"] = uid;
        notification["current_participants"] = redis->SCard(room_key);
        const std::string notification_text = notification.toStyledString();

        // 离开者通过 LEAVE_MEETING_RESPONSE 回到主界面；这里只通知会议内的其他用户移除成员。
        for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
            const auto meeting_session = it->lock();
            if (!meeting_session) {
                it = meeting_sessions.erase(it);
                continue;
            }

            if (meeting_session->GetUserId() != uid) {
                meeting_session->Send(ID_MEETING_MEMBER_LEFT, notification_text);
            }
            ++it;
        }

        if (meeting_sessions.empty()) {
            m_meeting_sessions.erase(sessions_it);
        }
    }

    // 离开会议只清理实时状态，不改变会议生命周期或参会记录。
    session->SetMeetingId(0);
    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_id);
}

void LogicSystem::EndMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_END_MEETING_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() || !root["meeting_id"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_id_text = root["meeting_id"].asString();
    std::uint64_t meeting_id = 0;
    try {
        std::size_t parsed_length = 0;
        meeting_id = std::stoull(meeting_id_text, &parsed_length);
        if (meeting_id == 0 || parsed_length != meeting_id_text.size()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    }
    catch (const std::exception&) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    MeetingInfo meeting_info;
    if (!MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }

    // 当前表结构未保存 co_host，先以创建者作为唯一可结束会议的主持人。
    if (meeting_info.creator_user_id != static_cast<std::uint64_t>(uid)) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }
    if (meeting_info.status != MeetingStatus::kInProgress) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }
    if (!MysqlMgr::GetInstance()->EndMeeting(meeting_id, uid)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_id);
    value["status"] = "ended";

    auto redis = RedisMgr::GetInstance();
    const std::string room_key = RoomMembersKey(meeting_id);
    std::vector<std::string> room_members;
    bool cleanup_ok = redis->SMembers(room_key, room_members);

    // 会议生命周期已经持久化为 ended，后续只做实时房间清理；清理失败不能把结束会议误报为失败。
    if (cleanup_ok) {
        for (const std::string& member_uid_text : room_members) {
            int member_uid = 0;
            try {
                member_uid = std::stoi(member_uid_text);
            }
            catch (const std::exception&) {
                cleanup_ok = false;
                continue;
            }

            cleanup_ok = redis->Del(RoomMemberStateKey(meeting_id, member_uid)) && cleanup_ok;
            cleanup_ok = redis->ZRem(ReconnectDeadlineKey(),
                                     ReconnectDeadlineMember(meeting_id, member_uid)) && cleanup_ok;
            cleanup_ok = redis->HSet("presence:" + member_uid_text, "status", "online") && cleanup_ok;
            cleanup_ok = redis->HSet("presence:" + member_uid_text, "meeting_id", "") && cleanup_ok;
        }

        cleanup_ok = redis->Del(room_key) && cleanup_ok;
    }
    value["cleanup_sync"] = cleanup_ok;

    Json::Value notification;
    notification["meeting_id"] = std::to_string(meeting_id);
    notification["status"] = "ended";
    const std::string notification_text = notification.toStyledString();

    const auto sessions_it = m_meeting_sessions.find(meeting_id);
    if (sessions_it == m_meeting_sessions.end()) {
        return;
    }

    for (const std::weak_ptr<Session>& item : sessions_it->second) {
        const auto meeting_session = item.lock();
        if (meeting_session) {
            meeting_session->Send(ID_MEETING_ENDED, notification_text);
            meeting_session->SetMeetingId(0);
        }
    }
    m_meeting_sessions.erase(sessions_it);
}

void LogicSystem::SendMeetingMessageHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_SEND_MEETING_MESSAGE_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["meeting_id"].isString() || !root["chat_type"].isString() ||
        !root["content"].isString() || !root["client_msg_id"].isString() ||
        !root.isMember("target_user_id")) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_id_text = root["meeting_id"].asString();
    const std::string chat_type = root["chat_type"].asString();
    const std::string content = root["content"].asString();
    const std::string client_msg_id = root["client_msg_id"].asString();
    value["meeting_id"] = meeting_id_text;
    value["client_msg_id"] = client_msg_id;
    if ((chat_type != "group" && chat_type != "private") || content.empty() || content.size() > 2000 ||
        client_msg_id.empty() || client_msg_id.size() > 128) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t target_user_id = 0;
    if (chat_type == "group") {
        if (!root["target_user_id"].isNull()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    } else {
        const Json::Value &target_value = root["target_user_id"];
        if (!target_value.isUInt() && !target_value.isUInt64() && !target_value.isInt()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        if (target_value.isInt() && target_value.asInt() <= 0) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        target_user_id = target_value.asUInt64();
        if (target_user_id == 0) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    }

    std::uint64_t meeting_id = 0;
    try {
        std::size_t parsed_length = 0;
        meeting_id = std::stoull(meeting_id_text, &parsed_length);
        if (meeting_id == 0 || parsed_length != meeting_id_text.size()) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
    }
    catch (const std::exception&) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // 只相信服务端 session 与 Redis 房间成员关系，不使用客户端传入的发送者身份。
    const std::string room_key = RoomMembersKey(meeting_id);
    if (!RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }
    if (chat_type == "private" &&
        !RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(target_user_id))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    MeetingInfo meeting_info;
    if (!MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }
    if (meeting_info.status == MeetingStatus::kEnded || meeting_info.status == MeetingStatus::kCancelled) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    std::string sender_name;
    if (!MysqlMgr::GetInstance()->GetUserDisplayNameById(uid, sender_name)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    // 聊天消息先写入 MySQL，再用数据库生成的主键和时间回包。
    MeetingMessageInfo stored_message;
    stored_message.meeting_id = meeting_id;
    stored_message.sender_user_id = uid;
    if (chat_type == "private") {
        stored_message.receiver_user_id = target_user_id;
    }
    stored_message.client_msg_id = client_msg_id;
    stored_message.content = content;
    stored_message.content_type = 0;

    // 先落库再推送，message_id 和 created_at 以后统一以 MySQL 为准。
    if (!MysqlMgr::GetInstance()->SaveMeetingMessage(stored_message)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value message_value = MeetingMessageToJson(stored_message, sender_name, true);

    value = message_value;
    value["error"] = ErrorCodes::SUCCESS;
    const std::string notification_text = message_value.toStyledString();

    // 私聊只推送给发送者和目标成员；群聊仍推送给当前房间内的所有连接。
    const auto sessions_it = m_meeting_sessions.find(meeting_id);
    if (sessions_it == m_meeting_sessions.end()) {
        return;
    }

    auto &meeting_sessions = sessions_it->second;
    for (auto it = meeting_sessions.begin(); it != meeting_sessions.end();) {
        const auto meeting_session = it->lock();
        if (!meeting_session) {
            it = meeting_sessions.erase(it);
            continue;
        }

        if (chat_type == "group" || meeting_session->GetUserId() == uid ||
            meeting_session->GetUserId() == static_cast<int>(target_user_id)) {
            meeting_session->Send(ID_MEETING_MESSAGE_PUSH, notification_text);
        }
        ++it;
    }
}

void LogicSystem::GetMeetingGroupMessagesHandler(std::shared_ptr<Session> session,
                                                 std::uint16_t&,
                                                 std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_GET_MEETING_GROUP_MESSAGES_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("meeting_id")) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t meeting_id = 0;
    if (!ReadUInt64Value(root["meeting_id"], meeting_id) || meeting_id == 0) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t before_message_id = 0;
    if (root.isMember("before_message_id") && !root["before_message_id"].isNull() &&
        !ReadUInt64Value(root["before_message_id"], before_message_id)) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint32_t limit = 50;
    if (root.isMember("limit") && !root["limit"].isNull()) {
        std::uint64_t request_limit = 0;
        if (!ReadUInt64Value(root["limit"], request_limit) || request_limit == 0) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        limit = static_cast<std::uint32_t>(request_limit > 100 ? 100 : request_limit);
    }

    value["meeting_id"] = std::to_string(meeting_id);
    value["before_message_id"] = Json::Value(static_cast<Json::UInt64>(before_message_id));
    value["limit"] = Json::Value(static_cast<Json::UInt>(limit));

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // 历史消息只能给当前房间成员读取，不能只相信客户端传入的 meeting_id。
    if (!RedisMgr::GetInstance()->SIsMember(RoomMembersKey(meeting_id), std::to_string(uid))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    std::vector<MeetingMessageInfo> messages;
    if (!MysqlMgr::GetInstance()->GetMeetingGroupMessages(
            meeting_id, before_message_id, limit, messages)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value json_messages(Json::arrayValue);
    for (const MeetingMessageInfo& stored_message : messages) {
        std::string sender_name;
        if (!MysqlMgr::GetInstance()->GetUserDisplayNameById(
                stored_message.sender_user_id, sender_name)) {
            sender_name = "用户 " + std::to_string(stored_message.sender_user_id);
        }

        json_messages.append(MeetingMessageToJson(
            stored_message, sender_name, stored_message.sender_user_id == uid));
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["messages"] = std::move(json_messages);
}

void LogicSystem::GetMeetingPrivateMessagesHandler(std::shared_ptr<Session> session,
                                                   std::uint16_t&,
                                                   std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        session->Send(ID_GET_MEETING_PRIVATE_MESSAGES_RESPONSE, value.toStyledString());
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("meeting_id") || !root.isMember("peer_user_id")) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t meeting_id = 0;
    std::uint64_t peer_user_id = 0;
    if (!ReadUInt64Value(root["meeting_id"], meeting_id) || meeting_id == 0 ||
        !ReadUInt64Value(root["peer_user_id"], peer_user_id) || peer_user_id == 0) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t before_message_id = 0;
    if (root.isMember("before_message_id") && !root["before_message_id"].isNull() &&
        !ReadUInt64Value(root["before_message_id"], before_message_id)) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint32_t limit = 50;
    if (root.isMember("limit") && !root["limit"].isNull()) {
        std::uint64_t request_limit = 0;
        if (!ReadUInt64Value(root["limit"], request_limit) || request_limit == 0) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        limit = static_cast<std::uint32_t>(request_limit > 100 ? 100 : request_limit);
    }

    value["meeting_id"] = std::to_string(meeting_id);
    value["peer_user_id"] = Json::Value(static_cast<Json::UInt64>(peer_user_id));
    value["before_message_id"] = Json::Value(static_cast<Json::UInt64>(before_message_id));
    value["limit"] = Json::Value(static_cast<Json::UInt>(limit));

    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id || static_cast<std::uint64_t>(uid) == peer_user_id ||
        peer_user_id > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    const std::string room_key = RoomMembersKey(meeting_id);
    // 私聊历史只允许当前会议房间内的双方读取，不能通过伪造 peer_user_id 看别人的对话。
    if (!RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid)) ||
        !RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(peer_user_id))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    std::vector<MeetingMessageInfo> messages;
    if (!MysqlMgr::GetInstance()->GetMeetingPrivateMessages(
            meeting_id,
            uid,
            static_cast<int>(peer_user_id),
            before_message_id,
            limit,
            messages)) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value json_messages(Json::arrayValue);
    for (const MeetingMessageInfo& stored_message : messages) {
        std::string sender_name;
        if (!MysqlMgr::GetInstance()->GetUserDisplayNameById(
                stored_message.sender_user_id, sender_name)) {
            sender_name = "用户 " + std::to_string(stored_message.sender_user_id);
        }

        json_messages.append(MeetingMessageToJson(
            stored_message, sender_name, stored_message.sender_user_id == uid));
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["messages"] = std::move(json_messages);
}

void LogicSystem::MediaOfferHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    bool need_reply = true;
    // 校验失败时直接用 answer 响应带回错误码；校验通过后交给 MediaServer 异步返回 answer。
    Defer defer([&value, session, &need_reply](){
        if (need_reply) {
            session->Send(ID_MEDIA_ANSWER_RESPONSE, value.toStyledString());
        }
    });

    // offer 必须带会议号、信令类型和 SDP；SDP 内容本身后续由 MediaServer 理解。
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("meeting_id") || !root["type"].isString() || !root["sdp"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t meeting_id = 0;
    if (!ReadUInt64Value(root["meeting_id"], meeting_id) || meeting_id == 0 ||
        root["type"].asString() != "offer" || root["sdp"].asString().empty()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    // 只相信登录后绑定在 Session 上的 uid，不使用客户端在 JSON 中自报的身份。
    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // Redis 房间成员表示当前实时在会；没有在房间里就不能发布媒体信令。
    const std::string room_key = RoomMembersKey(meeting_id);
    if (!RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // MySQL 会议状态负责生命周期判断，只有进行中的会议才允许建立媒体链路。
    MeetingInfo meeting_info;
    if (!MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }
    if (meeting_info.status != MeetingStatus::kInProgress) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    Json::Value media_request;
    const std::uint64_t previous_signal_id = session->GetMediaSignalId();
    if (previous_signal_id != 0) {
        m_media_signal_sessions.erase(previous_signal_id);
    }
    const std::uint64_t signal_id = m_next_media_signal_id++;
    session->SetMediaSignalId(signal_id);
    m_media_signal_sessions[signal_id] = session;

    // signal_id 只在两台服务之间使用，MediaServer 返回 answer/candidate 时靠它找回当前 TCP 连接。
    media_request["signal_id"] = static_cast<Json::UInt64>(signal_id);
    media_request["meeting_id"] = static_cast<Json::UInt64>(meeting_id);
    media_request["uid"] = uid;
    media_request["signal_type"] = "offer";
    media_request["sdp"] = root["sdp"].asString();

    if (!SendMediaSignal(media_request.toStyledString())) {
        m_media_signal_sessions.erase(signal_id);
        session->SetMediaSignalId(0);
        value["error"] = ErrorCodes::ERROR_NETWORK;
        return;
    }

    // answer 和 MediaServer 侧收集到的 candidate 由 UDS 读线程异步回填，当前请求无需同步响应。
    need_reply = false;
}

void LogicSystem::MediaCandidateHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    bool need_reply = true;
    // 校验失败时直接用 candidate 响应带回错误码；校验通过后再由 MediaServer 决定是否回传候选。
    Defer defer([&value, session, &need_reply](){
        if (need_reply) {
            session->Send(ID_MEDIA_CANDIDATE_RESPONSE, value.toStyledString());
        }
    });

    // candidate 信令只校验业务字段是否齐全，不在 RealtimeServer 解析 ICE 内容。
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("meeting_id") || !root["candidate"].isString() || !root["mid"].isString()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    std::uint64_t meeting_id = 0;
    if (!ReadUInt64Value(root["meeting_id"], meeting_id) || meeting_id == 0 ||
        root["candidate"].asString().empty() || root["mid"].asString().empty()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    // 媒体 candidate 必须来自当前登录连接，不能相信客户端传来的 uid。
    const int uid = session->GetUserId();
    if (uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }
    if (session->GetMeetingId() != meeting_id) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // 只有当前仍在 Redis 房间成员集合里的用户，才允许继续补充媒体候选地址。
    const std::string room_key = RoomMembersKey(meeting_id);
    if (!RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid))) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // 会议已经结束或还没开始时，不再接受新的媒体候选。
    MeetingInfo meeting_info;
    if (!MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info)) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }
    if (meeting_info.status != MeetingStatus::kInProgress) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    Json::Value media_request;
    // candidate 属于当前 offer 协商，沿用 offer 的 signal_id 才能让服务端产生的 candidate 回到同一客户端。
    media_request["signal_id"] = static_cast<Json::UInt64>(session->GetMediaSignalId());
    media_request["meeting_id"] = static_cast<Json::UInt64>(meeting_id);
    media_request["uid"] = uid;
    media_request["signal_type"] = "candidate";
    media_request["candidate"] = root["candidate"].asString();
    media_request["mid"] = root["mid"].asString();

    if (!SendMediaSignal(media_request.toStyledString())) {
        value["error"] = ErrorCodes::ERROR_NETWORK;
        return;
    }

    // 远端 candidate 不需要业务层确认；MediaServer 后续产生的本地 candidate 会异步返回。
    need_reply = false;
}

void LogicSystem::MediaSignalResponseHandler(std::shared_ptr<Session>, std::uint16_t&, std::string& message)
{
    Json::Reader reader;
    Json::Value root;
    std::uint64_t signal_id = 0;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("signal_id") || !root["signal_type"].isString() ||
        !ReadUInt64Value(root["signal_id"], signal_id)) {
        return;
    }

    const auto signal_it = m_media_signal_sessions.find(signal_id);
    if (signal_it == m_media_signal_sessions.end()) {
        return;
    }
    const auto client_session = signal_it->second.lock();
    if (!client_session) {
        m_media_signal_sessions.erase(signal_it);
        return;
    }

    const std::string signal_type = root["signal_type"].asString();
    Json::Value value;
    value["error"] = ErrorCodes::SUCCESS;
    if (signal_type == "answer" && root["sdp"].isString()) {
        // MediaServer 只给出 WebRTC 协商结果，客户端协议号仍由 RealtimeServer 统一决定。
        value["type"] = "answer";
        value["sdp"] = root["sdp"].asString();
        client_session->Send(ID_MEDIA_ANSWER_RESPONSE, value.toStyledString());
        return;
    }
    if (signal_type == "candidate" && root["candidate"].isString() && root["mid"].isString()) {
        value["candidate"] = root["candidate"].asString();
        value["mid"] = root["mid"].asString();
        client_session->Send(ID_MEDIA_CANDIDATE_RESPONSE, value.toStyledString());
    }
}
