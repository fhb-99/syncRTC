#include "service/LogicSystem.h"

#include "storage/RedisMgr.h"
#include "storage/MySqlMgr.h"
#include "common/data.h"

#include <iostream>
#include <utility>

#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>

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
        meeting_json["host_display_name"] = meeting.host_display_name;
        meeting_json["host_avatar_url"] = meeting.host_avatar_url;
        meeting_json["status"] = static_cast<Json::UInt>(meeting.status);
        meeting_json["requires_password"] = meeting.requires_password;
        meeting_json["max_participants"] = meeting.max_participants;
        meeting_json["scheduled_at"] = meeting.scheduled_at;
        meeting_list.append(std::move(meeting_json));
    }
    res["meetings"] = std::move(meeting_list);
    res["error"] = ErrorCodes::SUCCESS;
}
