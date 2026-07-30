#pragma once

#include "common/Singleton.h"
#include "common/global.h"
#include "net/Session.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

// 逻辑线程处理的最小消息单元，保存来源连接和已解出的完整协议帧。
struct LogicNode
{
    std::shared_ptr<Session> session;
    std::uint16_t id;
    std::string message;
};

using callback = std::function<void(std::shared_ptr<Session> session,
                                    std::uint16_t id,
                                    std::string message)>;

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();

    // Session 解出完整帧后，将同一类型的 LogicNode 投递到逻辑队列。
    void PostMsgToQue(std::shared_ptr<LogicNode> message);

private:
    LogicSystem();

    void DealMessage();

    void initHandlers();
    std::unordered_map<std::uint16_t, callback> maps;

    // 客户端登录处理
    void LoginHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 客户端创建会议处理
    void CreateMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 得到客户端历史会议信息处理
    void GetPastMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 处理用户的入会请求
    void JoinMeeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    bool m_stop;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread work_thread;
    std::queue<std::shared_ptr<LogicNode>> m_msg_que;
};
