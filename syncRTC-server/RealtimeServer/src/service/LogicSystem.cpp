#include "service/LogicSystem.h"

#include "storage/RedisMgr.h"
#include "storage/MySqlMgr.h"
#include "common/data.h"

#include <crypt.h>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

namespace {

constexpr unsigned long kBcryptCost = 12;

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

} // namespace

LogicSystem::LogicSystem()
    : m_stop(false)
{
    initHandlers();
    work_thread = std::thread(&LogicSystem::DealMessage, this);
}

LogicSystem::~LogicSystem()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cond.notify_all();

    if (work_thread.joinable()) {
        work_thread.join();
    }
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

    std::vector<std::string> members;
    if (!redis->SMembers(room_key, members)) {
        value["error"] = ErrorCodes::ERROR_REDIS;
        return;
    }

    // 入会成功后再绑定会议 ID，避免失败请求污染当前连接状态。
    session->SetMeetingId(meeting_info.meeting_id);
    auto& meeting_sessions = m_meeting_sessions[meeting_info.meeting_id];
    meeting_sessions.erase(std::remove_if(meeting_sessions.begin(), meeting_sessions.end(),
        [&session](const std::weak_ptr<Session>& item) {
            const auto active_session = item.lock();
            return !active_session || active_session == session;
        }), meeting_sessions.end());
    meeting_sessions.push_back(session);

    Json::Value member_array(Json::arrayValue);
    for (const auto& member : members) {
        try {
            member_array.append(static_cast<Json::UInt64>(std::stoull(member)));
        }
        catch (const std::exception&) {
            // 理论上成员都是 uid；这里兜底避免脏数据导致响应构造失败。
            member_array.append(member);
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
    value["member_uids"] = std::move(member_array);

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
        }
    }
}
