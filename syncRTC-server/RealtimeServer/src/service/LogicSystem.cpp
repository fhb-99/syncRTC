#include "service/LogicSystem.h"

#include "storage/RedisMgr.h"
#include "storage/MySqlMgr.h"
#include "common/data.h"
#include "config/ConfigMgr.h"

#include <array>
#include <chrono>
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
#include <sys/random.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr unsigned long kBcryptCost = 12;
constexpr int kAiErrorEmptyQuestion = 1001;
constexpr int kAiErrorRequestFailed = 2002;
// 被动断线后给客户端的重连宽限时间。到期仍未重新入会，才真正移出会议成员列表。
constexpr int kReconnectGraceSeconds = 45;
// 以下两个 ID 只在服务端内部队列使用，不会通过 TCP 发送给客户端。
// 它们将 I/O 线程发现的断线、timerfd 定时检查投递给串行业务上下文。
constexpr std::uint16_t kSessionDisconnectedEventId = 0xFFFE;
constexpr std::uint16_t kReconnectTimeoutCheckEventId = 0xFFFD;
// MediaServer 通过 UDS 返回的信令不会暴露给客户端，先投递到 LogicSystem 查找原 Session。
constexpr std::uint16_t kMediaSignalResponseEventId = 0xFFFC;
constexpr std::uint16_t kMediaConnectedEventId = 0xFFFB;
constexpr std::uint16_t kMediaDisconnectedEventId = 0xFFFA;

std::string NewResumeTicket()
{
    std::array<unsigned char, 32> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("生成恢复票据失败");
        offset += static_cast<std::size_t>(count);
    }
    const char* hex = "0123456789abcdef";
    std::string ticket;
    for (const auto byte : bytes) {
        ticket += hex[byte >> 4];
        ticket += hex[byte & 15];
    }
    return ticket;
}

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
    : m_control(std::make_shared<SerialCommandQueue>(m_scheduler, 16)),
      m_timer_queue(std::make_shared<SerialCommandQueue>(m_scheduler, 1)),
      m_media_client([this](MediaSignalClient::Event event, std::uint64_t epoch, std::string message) {
          PostMediaEvent(event, epoch, std::move(message));
      })
{
    auto ai_config = ConfigMgr::Init()["AIService"];
    const std::string ai_host = ai_config["Host"];
    const std::string ai_port = ai_config["Port"];
    if (!ai_host.empty() && !ai_port.empty()) {
        m_ai_stub = message::MeetingAiService::NewStub(
            grpc::CreateChannel(ai_host + ":" + ai_port, grpc::InsecureChannelCredentials()));
    }

    initHandlers();

}

LogicSystem::~LogicSystem()
{
    Shutdown();
}

void LogicSystem::Shutdown()
{
    // CServer 已停止 I/O；先拒绝新入口，等已接纳操作及其慢任务全部回投完成。
    { std::lock_guard<std::mutex> lock(m_registry_mutex); m_stopping = true; }
    m_scheduler.WaitIdle();
    // 工作线程的 MySQL thread_local 清理必须早于驱动等依赖的静态析构。
    m_storage_pool.Join();
    m_ai_pool.Join();
}

MediaSignalClient& LogicSystem::GetMediaSignalClient() { return m_media_client; }

void LogicSystem::initHandlers()
{
    // 注册表初始化后只读；所有 handler 由连接或会议的串行操作调用。
    maps[AUTH_LOGIN_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message){
        LoginHandler(op, room, session, id, message);
    };

    maps[ID_AI_ASK_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        AiAskHandler(op, room, session, id, message);
    };

    // 处理客户端的创建会议请求
    maps[ID_CREATE_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        CreateMeetingHandler(op, room, session, id, message);
    };

    // 处理客户端的得到历史会议请求
    maps[ID_PAST_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message){
        GetPastMeetingHandler(op, room, session, id, message);
    };

    // 处理入会
    maps[ID_JOIN_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        JoinMeeetingHandler(op, room, session, id, message);
    };

    maps[ID_RESUME_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        JoinMeeetingHandler(op, room, session, id, message);
    };

    maps[ID_START_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        StartMeetingHandler(op, room, session, id, message);
    };

    maps[ID_LEAVE_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        LeaveMeetingHandler(op, room, session, id, message);
    };

    maps[ID_END_MEETING_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        EndMeetingHandler(op, room, session, id, message);
    };

    maps[ID_SEND_MEETING_MESSAGE_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        SendMeetingMessageHandler(op, room, session, id, message);
    };

    maps[ID_GET_MEETING_GROUP_MESSAGES_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        GetMeetingGroupMessagesHandler(op, room, session, id, message);
    };

    maps[ID_GET_MEETING_PRIVATE_MESSAGES_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        GetMeetingPrivateMessagesHandler(op, room, session, id, message);
    };

    maps[ID_MEDIA_OFFER_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaOfferHandler(op, room, session, id, message);
    };

    maps[ID_MEDIA_CANDIDATE_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaCandidateHandler(op, room, session, id, message);
    };

    maps[ID_MEDIA_RENEGOTIATION_ANSWER_REQUEST] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaAnswerHandler(op, room, session, id, message);
    };

    maps[kMediaSignalResponseEventId] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        MediaSignalResponseHandler(op, room, session, id, message);
    };

    maps[kSessionDisconnectedEventId] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        SessionDisconnectedHandler(op, room, session, id, message);
    };

    maps[kReconnectTimeoutCheckEventId] = [this](Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t id, std::string message) {
        ReconnectTimeoutCheckHandler(op, room, session, id, message);
    };
}


void LogicSystem::AiAskHandler(Operation& op, Room&, std::shared_ptr<Session> session,
                               std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    // 统一使用 Defer 回包，下面任意分支都只需要填写错误或答案。
    Json::Value response;
    Defer defer([session, &response]() {
        if (std::uncaught_exceptions() > 0) response["error"] = ErrorCodes::ERROR_NETWORK;
        session->Send(ID_AI_ASK_RESPONSE, response.toStyledString());
    });

    // 登录状态来自服务端 Session；普通问答不需要当前会议 ID。
    if (session->GetUserId() <= 0) {
        response["error"] = ErrorCodes::ERROR_TOKEN;
        response["error_message"] = "请先登录后再提问。";
        return;
    }

    Json::Reader reader;
    Json::Value request_json;
    if (!reader.parse(message, request_json) || !request_json.isObject() ||
        !request_json["question"].isString()) {
        response["error"] = ErrorCodes::ERROR_JSON;
        response["error_message"] = "问题格式不正确。";
        return;
    }

    const std::string question = request_json["question"].asString();
    if (question.empty()) {
        response["error"] = kAiErrorEmptyQuestion;
        response["error_message"] = "问题不能为空。";
        return;
    }

    if (!m_ai_stub) {
        response["error"] = kAiErrorRequestFailed;
        response["error_message"] = "AI 服务暂时不可用，请稍后再试。";
        return;
    }

    // gRPC 请求只携带问题文本，RealtimeServer 不把会议状态传给 AIServer。
    message::AskAssistantRequest ai_request;
    ai_request.set_question(question);
    message::AskAssistantResponse ai_response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(65));

    // RPC 在独立 AI 池运行；等待期间保留 active_op，释放业务 worker。
    const grpc::Status status = op.Await(m_ai_pool, [&] {
        return m_ai_stub->AskAssistant(&context, ai_request, &ai_response);
    });
    if (!status.ok()) {
        response["error"] = kAiErrorRequestFailed;
        response["error_message"] = "AI 服务暂时不可用，请稍后再试。";
        return;
    }

    response["error"] = ai_response.error();
    if (ai_response.error() == 0) {
        response["answer"] = ai_response.answer();
        return;
    }

    response["error_message"] = ai_response.error_message();
}


void LogicSystem::LoginHandler(Operation& op, Room&, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    Json::Value res;
    Defer defer([session, &res] {
        if (std::uncaught_exceptions() > 0) res["error"] = ErrorCodes::ERROR_NETWORK;
        session->Send(AUTH_LOGIN_RESPONSE, res.toStyledString());
    });
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() || !root["uid"].isInt() ||
        !root["token"].isString() || !root["email"].isString()) {
        res["error"] = ErrorCodes::ERROR_JSON;
        return;
    }
    const auto uid = root["uid"].asInt();
    const auto token = root["token"].asString();
    const auto email = root["email"].asString();

    // 从redis当中取出token
    std::string tmp;
    const auto lookup = op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->Lookup("auth::session:" + token, tmp); });
    if (lookup != RedisMgr::LookupResult::Found) {
        res["error"] = lookup == RedisMgr::LookupResult::Missing
            ? ErrorCodes::ERROR_TOKEN : ErrorCodes::ERROR_REDIS;
        return;
    }

    Json::Value session_info;
    Json::Reader session_reader;
    if (!session_reader.parse(tmp, session_info)) {
        res["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    const int user_id = session_info["uid"].asInt();
    if (user_id <= 0 || uid != user_id ||
        (session->GetUserId() > 0 && session->GetUserId() != user_id)) {
        res["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    UserInfo user_info;
    bool is_user = op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetUserInfo(email, user_info); });
    if(!is_user || user_info.uid != user_id || user_info.username.empty()) {
        std::cout << "Get user info failed" << std::endl;
		res["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    res["email"] = user_info.email;
    res["username"] = user_info.username;

    // 后续还要查询会议参与表，来返回给客户端展示界面的数据
    std::vector<RecentMeetingInfo> meetings;
    bool is_recent = op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingRecently(user_id, meetings); });
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
    if (session->IsClosed()) return;
    session->SetAuthToken(token);
    session->SetUserId(user_id);
    res["capabilities"].append("meeting_resume_v1");
    res["capabilities"].append("heartbeat_v1");
    res["capabilities"].append("heartbeat_v2");
    res["error"] = ErrorCodes::SUCCESS;
}



void LogicSystem::CreateMeetingHandler(Operation& op, Room&, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
        create_info.password_hash = op.Await(m_storage_pool, [&] { return HashMeetingPassword(password); });
        if (create_info.password_hash.empty()) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }
    }

    RecentMeetingInfo meeting;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->CreateMeeting(create_info, meeting); })) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }
    // 列表顺序和数量由 MySQL 统一决定，客户端直接用返回结果替换当前列表。
    std::vector<RecentMeetingInfo> meetings;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingRecently(user_id, meetings); })) {
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




void LogicSystem::GetPastMeetingHandler(Operation& op, Room&, std::shared_ptr<Session> session, std::uint16_t&, std::string&)
{
    if (!session) {
        return;
    }


    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
        session->Send(ID_PAST_MEETING_RESPONSE, value.toStyledString());
    });

    const int user_id = session->GetUserId();
    if (user_id <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    std::vector<HistoryMeetingInfo> meetings;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetHistoryMeeting(user_id, meetings); })) {
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



void LogicSystem::JoinMeeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t& id, std::string& message)
{
    if (!session) {
        return;
    }

    const bool resuming = id == ID_RESUME_MEETING_REQUEST;
    Json::Value value;
    Defer defer([&value, session, resuming](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
        if (!session->Send(resuming ? ID_RESUME_MEETING_RESPONSE : ID_JOIN_MEETING_RESPONSE,
                           value.toStyledString())) session->RequestClose();
    });

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        (!resuming && !root["meeting_code"].isString())) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const std::string meeting_code = root["meeting_code"].asString();
    if(!resuming && meeting_code.empty()) {
        value["error"] = ErrorCodes::ERROR_JSON;
        return;
    }

    const int uid = session->GetUserId();
    if(uid <= 0) {
        value["error"] = ErrorCodes::ERROR_TOKEN;
        return;
    }

    MeetingInfo meeting_info;
    bool storage_error = false;
    if(!op.Await(m_storage_pool, [&] {
        return resuming ? MysqlMgr::GetInstance()->GetMeetingInfoById(room.id, meeting_info, &storage_error)
                        : MysqlMgr::GetInstance()->GetMeetingInfoByCode(meeting_code, meeting_info);
    })) {
        value["error"] = storage_error ? ErrorCodes::ERROR_MYSQL : ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }

    if (meeting_info.meeting_id != room.id) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }

    // 加入会议只进入房间，不改变会议生命周期；已取消或已结束的会议都不能重新入会。
    if (meeting_info.status == MeetingStatus::kCancelled ||
        meeting_info.status == MeetingStatus::kEnded) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    std::uint64_t known_generation = 0;
    if (resuming) {
        const auto member = room.members.find(uid);
        if (!root["request_id"].isString() || root["request_id"].asString().empty() ||
            root["request_id"].asString().size() > 64 || !root["resume_ticket"].isString() ||
            !ReadUInt64Value(root["membership_generation"], known_generation)) {
            value["error"] = ErrorCodes::ERROR_JSON;
            return;
        }
        value["request_id"] = root["request_id"];
        // 只允许原已验证设备会话使用本成员的票据；设备 ID 本身不能授予接管权。
        if (member == room.members.end() || member->second.ticket != root["resume_ticket"].asString() ||
            member->second.auth_token.empty() || member->second.auth_token != session->GetAuthToken() ||
            (member->second.deadline != std::chrono::steady_clock::time_point{} &&
             member->second.deadline <= std::chrono::steady_clock::now()) ||
            (known_generation != member->second.generation &&
             !(known_generation < member->second.generation &&
               member->second.last_request == root["request_id"].asString()))) {
            value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
            return;
        }
    }

    if (!resuming && meeting_info.requires_password) {
        if (!root["password"].isString()) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }

        std::string password_hash;
        if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingPasswordHash(meeting_info.meeting_id,
                                                             password_hash); }) ||
            !op.Await(m_storage_pool, [&] { return CheckMeetingPassword(root["password"].asString(), password_hash); })) {
            value["error"] = ErrorCodes::ERROR_PASSWORD;
            return;
        }
    }

    const std::string uid_str = std::to_string(uid);
    const std::string room_key = RoomMembersKey(meeting_info.meeting_id);
    const std::string member_state_key = RoomMemberStateKey(meeting_info.meeting_id, uid);
    const std::string deadline_member = ReconnectDeadlineMember(meeting_info.meeting_id, uid);
    auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });
    const std::string role =
        meeting_info.creator_user_id == static_cast<std::uint64_t>(uid) ? "host" : "participant";


    // Redis 保存当前正在房间里的成员；它不是历史记录，只服务实时通信。
    std::vector<std::string> members;
    if (!op.Await(m_storage_pool, [&] { return redis->SMembers(room_key, members); })) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }
    const bool already_in_room = std::find(members.begin(), members.end(), uid_str) != members.end();
    const int current_count = static_cast<int>(members.size());
    if (!already_in_room && current_count >= meeting_info.max_participants) {
        value["error"] = ErrorCodes::ERROR_MEETING_FULL;
        return;
    }

    if (resuming && !already_in_room) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }
    bool is_reconnected = resuming;
    if (already_in_room && !resuming) {
        if (op.Await(m_storage_pool, [&] { return redis->HGet(member_state_key, "room_state"); }) != "reconnecting") {
            // 同一用户在会议中已有活跃连接，不允许第二台设备同时参会。
            value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
            return;
        }

        try {
            const std::time_t deadline = std::stoll(op.Await(m_storage_pool, [&] { return redis->HGet(member_state_key, "reconnect_deadline_at"); }));
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

    // 在任何入会写入前准备票据，避免随机源失败留下无凭据成员。
    const std::string new_ticket = resuming ? room.members.at(uid).ticket : NewResumeTicket();
    if(!op.Await(m_storage_pool, [&] { return redis->SAdd(room_key, uid_str); })) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    // MySQL 保存“这个用户参加过这场会议”的事实，后续历史会议就靠它查询。
    if (!resuming && !op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->UpdateMeetingPart(meeting_info, uid); })) {
        if (!already_in_room) {
            op.Await(m_storage_pool, [&] { return redis->SRem(room_key, uid_str); });
        }
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    const std::string presence_key = "presence:" + uid_str;
    if (!op.Await(m_storage_pool, [&] { return redis->HSet(presence_key, "status", "in_meeting"); }) ||
        !op.Await(m_storage_pool, [&] { return redis->HSet(presence_key, "meeting_id", std::to_string(meeting_info.meeting_id)); })) {
        if (!already_in_room) {
            op.Await(m_storage_pool, [&] { return redis->SRem(room_key, uid_str); });
        }
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    if (!already_in_room && !op.Await(m_storage_pool, [&] { return redis->HSet(member_state_key, "room_state", "active"); })) {
        op.Await(m_storage_pool, [&] { return redis->SRem(room_key, uid_str); });
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }
    if (is_reconnected && !op.Await(m_storage_pool, [&] {
            return redis->ActivateMember(meeting_info.meeting_id, uid);
        })) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    if (!op.Await(m_storage_pool, [&] { return redis->SMembers(room_key, members); })) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    // 先绑定会议归属；即使连接已关闭，排在本请求后的断线终结事件仍会按同一会议队列收尾。
    auto& membership = room.members[uid];
    const auto previous = membership.session.lock();
    if (previous && previous != session) {
        ReleaseMediaSession(previous);
        previous->SetMeetingId(0);
        previous->RequestClose();
    }
    if (!resuming) {
        membership = Membership{};
        membership.ticket = new_ticket;
        membership.auth_token = session->GetAuthToken();
    } else if (previous != session) {
        ++membership.generation;
    }
    membership.session = session;
    membership.deadline = {};
    membership.last_request = resuming ? root["request_id"].asString() : "";
    value["resume_ticket"] = membership.ticket;
    value["membership_generation"] = std::to_string(membership.generation);

    // 入会成功后再绑定会议 ID，避免失败请求污染当前连接状态。
    session->SetMeetingId(meeting_info.meeting_id);
    auto& meeting_sessions = room.sessions;
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
            const std::string room_state = op.Await(m_storage_pool, [&] { return redis->HGet(
                RoomMemberStateKey(meeting_info.meeting_id, static_cast<int>(member_uid)), "room_state"); });
            member_value["room_state"] = room_state.empty() ? "active" : room_state;

            std::string member_name;
            if (op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetUserDisplayNameById(
                    static_cast<int>(member_uid), member_name); })) {
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

void LogicSystem::StartMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); })) {
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
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->StartMeeting(meeting_id, uid); })) {
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

    if (room.sessions.empty()) {
        return;
    }

    auto& meeting_sessions = room.sessions;
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

void LogicSystem::SessionDisconnectedHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string&)
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
    if (room.sessions.empty()) {
        ReleaseMediaSession(session);
        session->SetMeetingId(0);
        return;
    }

    auto& meeting_sessions = room.sessions;
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

    // 只移除本次关闭的本地投递通道。room.sessions 仅用于本进程广播，
    // 不代表 Redis 中的会议成员资格。
    meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
        [&session](const std::weak_ptr<Session>& item) {
            const auto active_session = item.lock();
            return !active_session || active_session == session;
        }), meeting_sessions.end());

    const auto member = room.members.find(uid);
    if (member != room.members.end() && member->second.session.lock() == session) {
        member->second.deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(kReconnectGraceSeconds);
    }
    std::string notification_text;
    if (contains_disconnected_session && !has_other_connection && uid > 0) {
        const std::string uid_text = std::to_string(uid);
        const std::string room_key = RoomMembersKey(meeting_id);
        const std::string member_state_key = RoomMemberStateKey(meeting_id, uid);
        const std::string deadline_member = ReconnectDeadlineMember(meeting_id, uid);
        const auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });

        // 被动断线时保留 room:{meeting_id}:members 中的 uid，只写入三类重连信息：
        // 1. member hash 的 room_state=reconnecting，供客户端和重连请求识别；
        // 2. member hash 的 reconnect_deadline_at，供重连时判断是否仍在宽限期；
        // 3. 全局 ZSet 的截止记录，供定时器高效筛选超时成员。
        // 三项均成功后才构造广播，防止客户端已看到“重连中”而服务端没有超时依据。
        if (op.Await(m_storage_pool, [&] { return redis->SIsMember(room_key, uid_text); }) &&
            op.Await(m_storage_pool, [&] { return redis->HGet(member_state_key, "room_state"); }) != "reconnecting") {
            const std::time_t deadline = std::time(nullptr) + kReconnectGraceSeconds;
            if (op.Await(m_storage_pool, [&] { return redis->HSet(member_state_key, "room_state", "reconnecting"); }) &&
                op.Await(m_storage_pool, [&] { return redis->HSet(member_state_key, "reconnect_deadline_at", std::to_string(deadline)); }) &&
                op.Await(m_storage_pool, [&] { return redis->ZAdd(ReconnectDeadlineKey(), deadline_member, static_cast<double>(deadline)); })) {
                Json::Value notification;
                notification["meeting_id"] = std::to_string(meeting_id);
                notification["user_id"] = uid;
                notification["room_state"] = "reconnecting";
                notification["reconnect_deadline_at"] = static_cast<Json::Int64>(deadline);
                notification["current_participants"] = op.Await(m_storage_pool, [&] { return redis->SCard(room_key); });
                notification_text = notification.toStyledString();
            } else {
                // 写入未完整成功时恢复 active，并清除半成品截止记录；否则成员可能
                // 永久卡在 reconnecting，或被定时器错误移出会议。
                op.Await(m_storage_pool, [&] { return redis->HSet(member_state_key, "room_state", "active"); });
                op.Await(m_storage_pool, [&] { return redis->HSet(member_state_key, "reconnect_deadline_at", ""); });
                op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), deadline_member); });
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
        room.sessions.clear();
    }

    // 清除旧 Session 的会议上下文。若关闭事件被重复投递，meeting_id 为 0 会使
    // 本函数直接返回，从而保证对同一条断线连接的处理幂等。
    // 宽限期只保留业务成员资格，失去信令通道的旧媒体实例立即释放。
    ReleaseMediaSession(session);
    session->SetMeetingId(0);
}

void LogicSystem::ReconnectTimeoutCheckHandler(Operation& op, Room&, std::shared_ptr<Session>, std::uint16_t&, std::string&)
{
    const auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });
    std::vector<std::string> expired_members;
    const auto now = std::time(nullptr);
    if (!op.Await(m_storage_pool, [&] { return redis->ZRangeByScore(ReconnectDeadlineKey(), static_cast<double>(now), expired_members); })) return;
    for (const auto& member : expired_members) {
        const auto colon = member.find(':');
        std::uint64_t meeting = 0;
        try { if (colon != std::string::npos) meeting = std::stoull(member.substr(0, colon)); }
        catch (const std::exception&) {}
        if (meeting == 0) { op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), member); }); continue; }
        // 队列满时保留 Redis 截止记录，下次扫描重试，不能从别的线程直接清理会议。
        SubmitRoom(meeting, [this, member](Operation& operation, Room& target) {
            ExpireMemberHandler(operation, target, member);
        });
    }
}

void LogicSystem::ExpireMemberHandler(Operation& op, Room& room, const std::string& deadline_member)
{
    const auto meeting_id = room.id;
    const auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });
    const auto now = std::time(nullptr);
    int uid = 0;
    try { uid = std::stoi(deadline_member.substr(deadline_member.find(':') + 1)); }
    catch (const std::exception&) {}
    if (uid <= 0) { op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), deadline_member); }); return; }
    // 本进程签发的票据用单调时间保底，墙钟校时不能提前耗尽 45 秒宽限期。
    const auto local_member = room.members.find(uid);
    if (local_member != room.members.end() &&
        local_member->second.deadline > std::chrono::steady_clock::now()) return;
    const std::string member_state_key = RoomMemberStateKey(meeting_id, uid);
    const std::string deadline_text = op.Await(m_storage_pool, [&] { return redis->HGet(member_state_key, "reconnect_deadline_at"); });
    std::time_t deadline = 0;
    try {
        deadline = std::stoll(deadline_text);
    }
    catch (const std::exception&) {}
    if (deadline == 0) {
        op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), deadline_member); });
        return;
    }

    if (deadline > now) return; // 旧扫描结果不能删除后来断线产生的新截止记录。
    if (op.Await(m_storage_pool, [&] { return redis->HGet(member_state_key, "room_state"); }) != "reconnecting") {
        // 用户已经重连，或重连期间生成了新的截止时间。这条到期索引已失效，
        // 仅清理索引，不能删除当前仍有效的会议成员资格。
        op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), deadline_member); });
        return;
    }

    const std::string uid_text = std::to_string(uid);
    const std::string room_key = RoomMembersKey(meeting_id);
    // 宽限期真正到期后才移除成员：删除房间集合、超时索引和成员 hash，并将
    // presence 恢复为普通 online。这里不修改 meetings 表或会议生命周期，
    // 会议是否结束仍只能由主持人发起结束会议请求。
    if (!op.Await(m_storage_pool, [&] { return redis->SRem(room_key, uid_text); }) || !op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), deadline_member); })) {
        std::cerr << "清理重连超时成员失败，meeting_id=" << meeting_id
                  << ", uid=" << uid << std::endl;
        return;
    }

    op.Await(m_storage_pool, [&] { return redis->Del(member_state_key); });
    op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + uid_text, "status", "online"); });
    op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + uid_text, "meeting_id", ""); });

    room.members.erase(uid);
    ReleaseUser(uid, meeting_id);

    Json::Value notification;
    notification["meeting_id"] = std::to_string(meeting_id);
    notification["user_id"] = uid;
    notification["current_participants"] = op.Await(m_storage_pool, [&] { return redis->SCard(room_key); });
    const std::string notification_text = notification.toStyledString();

    // timeout 用户在断线时已经从本地连接列表移除，因此仅向同会议的现存连接
    // 广播 MEMBER_TIMEOUT_LEFT，让客户端删除该成员并刷新成员列表。
    if (room.sessions.empty()) {
        return;
    }

    auto& meeting_sessions = room.sessions;
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
        room.sessions.clear();
    }
}

void LogicSystem::LeaveMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
        MeetingInfo meeting_info;
        if (op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); }) &&
            meeting_info.status == MeetingStatus::kEnded) {
            // 结束会议会先清空会话中的 meeting_id，兼容旧客户端随后发出的离会请求。
            value["error"] = ErrorCodes::SUCCESS;
            value["meeting_id"] = std::to_string(meeting_id);
            return;
        }
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    const std::string uid_text = std::to_string(uid);
    const std::string room_key = RoomMembersKey(meeting_id);
    auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });
    if (!op.Await(m_storage_pool, [&] { return redis->SRem(room_key, uid_text); }) ||
        !op.Await(m_storage_pool, [&] { return redis->Del(RoomMemberStateKey(meeting_id, uid)); }) ||
        !op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(), ReconnectDeadlineMember(meeting_id, uid)); }) ||
        !op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + uid_text, "status", "online"); }) ||
        !op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + uid_text, "meeting_id", ""); })) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    if (!room.sessions.empty()) {
        auto& meeting_sessions = room.sessions;
        meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
            [&session](const std::weak_ptr<Session>& item) {
                const auto active_session = item.lock();
                return !active_session || active_session == session;
            }), meeting_sessions.end());

        Json::Value notification;
        notification["meeting_id"] = std::to_string(meeting_id);
        notification["user_id"] = uid;
        notification["current_participants"] = op.Await(m_storage_pool, [&] { return redis->SCard(room_key); });
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
            room.sessions.clear();
        }
    }

    ReleaseMediaSession(session);

    // 离开会议只清理实时状态，不改变会议生命周期或参会记录。
    room.members.erase(uid);
    ReleaseUser(uid, meeting_id);
    session->SetMeetingId(0);
    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_id);
}

void LogicSystem::EndMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); })) {
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
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->EndMeeting(meeting_id, uid); })) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["meeting_id"] = std::to_string(meeting_id);
    value["status"] = "ended";

    auto redis = op.Await(m_storage_pool, [] { return RedisMgr::GetInstance(); });
    const std::string room_key = RoomMembersKey(meeting_id);
    std::vector<std::string> room_members;
    bool cleanup_ok = op.Await(m_storage_pool, [&] { return redis->SMembers(room_key, room_members); });

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

            cleanup_ok = op.Await(m_storage_pool, [&] { return redis->Del(RoomMemberStateKey(meeting_id, member_uid)); }) && cleanup_ok;
            cleanup_ok = op.Await(m_storage_pool, [&] { return redis->ZRem(ReconnectDeadlineKey(),
                                     ReconnectDeadlineMember(meeting_id, member_uid)); }) && cleanup_ok;
            cleanup_ok = op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + member_uid_text, "status", "online"); }) && cleanup_ok;
            cleanup_ok = op.Await(m_storage_pool, [&] { return redis->HSet("presence:" + member_uid_text, "meeting_id", ""); }) && cleanup_ok;
        }

        cleanup_ok = op.Await(m_storage_pool, [&] { return redis->Del(room_key); }) && cleanup_ok;
    }
    value["cleanup_sync"] = cleanup_ok;
    room.members.clear();
    ReleaseRoomUsers(meeting_id);

    Json::Value notification;
    notification["meeting_id"] = std::to_string(meeting_id);
    notification["status"] = "ended";
    const std::string notification_text = notification.toStyledString();

    if (room.sessions.empty()) {
        return;
    }

    for (const std::weak_ptr<Session>& item : room.sessions) {
        const auto meeting_session = item.lock();
        if (meeting_session) {
            meeting_session->Send(ID_MEETING_ENDED, notification_text);
            ReleaseMediaSession(meeting_session);
            meeting_session->SetMeetingId(0);
        }
    }
    room.sessions.clear();
}

void LogicSystem::SendMeetingMessageHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }
    if (chat_type == "private" &&
        !op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(target_user_id)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    MeetingInfo meeting_info;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }
    if (meeting_info.status == MeetingStatus::kEnded || meeting_info.status == MeetingStatus::kCancelled) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    std::string sender_name;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetUserDisplayNameById(uid, sender_name); })) {
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
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->SaveMeetingMessage(stored_message); })) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value message_value = MeetingMessageToJson(stored_message, sender_name, true);

    value = message_value;
    value["error"] = ErrorCodes::SUCCESS;
    const std::string notification_text = message_value.toStyledString();

    // 私聊只推送给发送者和目标成员；群聊仍推送给当前房间内的所有连接。
    if (room.sessions.empty()) {
        return;
    }

    auto &meeting_sessions = room.sessions;
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

void LogicSystem::GetMeetingGroupMessagesHandler(Operation& op, Room&, std::shared_ptr<Session> session,
                                                 std::uint16_t&,
                                                 std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(RoomMembersKey(meeting_id), std::to_string(uid)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    std::vector<MeetingMessageInfo> messages;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingGroupMessages(
            meeting_id, before_message_id, limit, messages); })) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value json_messages(Json::arrayValue);
    for (const MeetingMessageInfo& stored_message : messages) {
        std::string sender_name;
        if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetUserDisplayNameById(
                stored_message.sender_user_id, sender_name); })) {
            sender_name = "用户 " + std::to_string(stored_message.sender_user_id);
        }

        json_messages.append(MeetingMessageToJson(
            stored_message, sender_name, stored_message.sender_user_id == uid));
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["messages"] = std::move(json_messages);
}

void LogicSystem::GetMeetingPrivateMessagesHandler(Operation& op, Room&, std::shared_ptr<Session> session,
                                                   std::uint16_t&,
                                                   std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    Defer defer([&value, session](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid)); }) ||
        !op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(peer_user_id)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    std::vector<MeetingMessageInfo> messages;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingPrivateMessages(
            meeting_id,
            uid,
            static_cast<int>(peer_user_id),
            before_message_id,
            limit,
            messages); })) {
        value["error"] = ErrorCodes::ERROR_MYSQL;
        return;
    }

    Json::Value json_messages(Json::arrayValue);
    for (const MeetingMessageInfo& stored_message : messages) {
        std::string sender_name;
        if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetUserDisplayNameById(
                stored_message.sender_user_id, sender_name); })) {
            sender_name = "用户 " + std::to_string(stored_message.sender_user_id);
        }

        json_messages.append(MeetingMessageToJson(
            stored_message, sender_name, stored_message.sender_user_id == uid));
    }

    value["error"] = ErrorCodes::SUCCESS;
    value["messages"] = std::move(json_messages);
}

void LogicSystem::MediaOfferHandler(Operation& op, Room&, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    bool need_reply = true;
    // 校验失败时直接用 answer 响应带回错误码；校验通过后交给 MediaServer 异步返回 answer。
    Defer defer([&value, session, &need_reply](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // MySQL 会议状态负责生命周期判断，只有进行中的会议才允许建立媒体链路。
    MeetingInfo meeting_info;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); })) {
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
        RemoveSignal(previous_signal_id);
    }
    const std::uint64_t signal_id = m_next_media_signal_id++;
    session->SetMediaSignalId(signal_id);
    const auto epoch = RegisterSignal(signal_id, session, meeting_id);

    // signal_id 只在两台服务之间使用，MediaServer 返回 answer/candidate 时靠它找回当前 TCP 连接。
    media_request["signal_id"] = static_cast<Json::UInt64>(signal_id);
    media_request["meeting_id"] = static_cast<Json::UInt64>(meeting_id);
    media_request["uid"] = uid;
    media_request["signal_type"] = "offer";
    media_request["sdp"] = root["sdp"].asString();

    if (!SendMediaSignal(media_request.toStyledString(), epoch)) {
        RemoveSignal(signal_id);
        session->SetMediaSignalId(0);
        value["error"] = ErrorCodes::ERROR_NETWORK;
        return;
    }

    // answer 和 MediaServer 侧收集到的 candidate 由单 I/O 线程接收并回投会议队列，当前请求无需同步响应。
    need_reply = false;
}

void LogicSystem::MediaAnswerHandler(Operation& op, Room&, std::shared_ptr<Session> session,
                                     std::uint16_t&,
                                     std::string& message)
{
    if (!session) {
        return;
    }

    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("meeting_id") || !root["type"].isString() || !root["sdp"].isString()) {
        return;
    }

    std::uint64_t meeting_id = 0;
    if (!ReadUInt64Value(root["meeting_id"], meeting_id) || meeting_id == 0 ||
        root["type"].asString() != "answer" || root["sdp"].asString().empty()) {
        return;
    }

    // Answer 只能由当前已登录、仍处于该会议中的客户端返回。
    // uid 继续取 Session 中的可信身份，不接受客户端在 JSON 中自行指定。
    const int uid = session->GetUserId();
    if (uid <= 0 || session->GetMeetingId() != meeting_id ||
        !op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(RoomMembersKey(meeting_id), std::to_string(uid)); })) {
        return;
    }

    const std::uint64_t signal_id = session->GetMediaSignalId();
    const auto signal = FindSignal(signal_id);
    if (signal_id == 0 || signal.session.lock() != session ||
        signal.epoch != m_media_epoch.load()) {
        return;
    }

    Json::Value media_request;
    // 服务端主动 Offer 沿用初次媒体协商的 signal_id，因此客户端 Answer 也沿用该标识。
    // MediaServer 收到后即可在对应会议和 uid 下找到原 PeerConnection。
    media_request["signal_id"] = static_cast<Json::UInt64>(signal_id);
    media_request["meeting_id"] = static_cast<Json::UInt64>(meeting_id);
    media_request["uid"] = uid;
    media_request["signal_type"] = "answer";
    media_request["sdp"] = root["sdp"].asString();
    SendMediaSignal(media_request.toStyledString(), signal.epoch);
}

void LogicSystem::MediaCandidateHandler(Operation& op, Room&, std::shared_ptr<Session> session, std::uint16_t&, std::string& message)
{
    if (!session) {
        return;
    }

    Json::Value value;
    bool need_reply = true;
    // 校验失败时直接用 candidate 响应带回错误码；校验通过后再由 MediaServer 决定是否回传候选。
    Defer defer([&value, session, &need_reply](){
        if (std::uncaught_exceptions() > 0) value["error"] = ErrorCodes::ERROR_NETWORK;
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
    if (!op.Await(m_storage_pool, [&] { return RedisMgr::GetInstance()->SIsMember(room_key, std::to_string(uid)); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_ACCESS;
        return;
    }

    // 会议已经结束或还没开始时，不再接受新的媒体候选。
    MeetingInfo meeting_info;
    if (!op.Await(m_storage_pool, [&] { return MysqlMgr::GetInstance()->GetMeetingInfoById(meeting_id, meeting_info); })) {
        value["error"] = ErrorCodes::ERROR_MEETING_NOT_FOUND;
        return;
    }
    if (meeting_info.status != MeetingStatus::kInProgress) {
        value["error"] = ErrorCodes::ERROR_MEETING_STATUS;
        return;
    }

    Json::Value media_request;
    // 断线清理后必须先建立新的 Offer 关联，不能把旧 Candidate 以零编号发往新连接。
    const auto signal_id = session->GetMediaSignalId();
    const auto signal = FindSignal(signal_id);
    if (signal_id == 0 || signal.session.lock() != session || signal.epoch != m_media_epoch.load()) {
        value["error"] = ErrorCodes::ERROR_NETWORK;
        return;
    }
    media_request["signal_id"] = static_cast<Json::UInt64>(signal_id);
    media_request["meeting_id"] = static_cast<Json::UInt64>(meeting_id);
    media_request["uid"] = uid;
    media_request["signal_type"] = "candidate";
    media_request["candidate"] = root["candidate"].asString();
    media_request["mid"] = root["mid"].asString();

    if (!SendMediaSignal(media_request.toStyledString(), signal.epoch)) {
        value["error"] = ErrorCodes::ERROR_NETWORK;
        return;
    }

    // 远端 candidate 不需要业务层确认；MediaServer 后续产生的本地 candidate 会异步返回。
    need_reply = false;
}

void LogicSystem::MediaSignalResponseHandler(Operation&, Room& room, std::shared_ptr<Session>, std::uint16_t&, std::string& message)
{
    Json::Reader reader;
    Json::Value root;
    std::uint64_t signal_id = 0;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root.isMember("signal_id") || !root["signal_type"].isString() ||
        !ReadUInt64Value(root["signal_id"], signal_id)) {
        return;
    }

    const auto signal = FindSignal(signal_id);
    if (signal.epoch == 0 || signal.epoch != m_media_epoch.load()) {
        return;
    }
    const auto client_session = signal.session.lock();
    if (!client_session) {
        RemoveSignal(signal_id);
        return;
    }

    if (client_session->IsClosed() || client_session->GetMeetingId() != room.id ||
        client_session->GetMediaSignalId() != signal_id || !m_media_client.IsCurrent(signal.epoch)) return;
    const std::string signal_type = root["signal_type"].asString();
    Json::Value value;
    value["error"] = ErrorCodes::SUCCESS;
    if (signal_type == "offer" && root["sdp"].isString()) {
        // 新成员带来新的消费 Track 后，MediaServer 主动生成 Offer。
        // RealtimeServer 只根据 signal_id 找回客户端连接，并使用独立协议号推送。
        value["type"] = "offer";
        value["sdp"] = root["sdp"].asString();
        client_session->Send(ID_MEDIA_RENEGOTIATION_OFFER, value.toStyledString());
        return;
    }
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
