#pragma once

#include "common/Singleton.h"
#include "common/global.h"
#include "net/Session.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

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

    // CServer 发现 TCP 连接关闭后调用。断线清理仍在逻辑线程执行，避免 I/O 线程直接修改会议状态。
    void PostSessionDisconnected(std::shared_ptr<Session> session);
    // timerfd 每秒调用一次；超时清理统一进入逻辑线程执行。
    void PostReconnectTimeoutCheck();

    // 启动到 MediaServer 的单条 UDS 连接，offer、answer 与 candidate 均经此连接转发。
    void StartMediaSignalClient(const std::string& socket_path);

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
    void StartMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void LeaveMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void EndMeetingHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void SendMeetingMessageHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void GetMeetingGroupMessagesHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void GetMeetingPrivateMessagesHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    // 媒体信令处理先预留接口，后续再接入 MediaServer 转发链路。
    void MediaOfferHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaAnswerHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaCandidateHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaSignalResponseHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void SessionDisconnectedHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void ReconnectTimeoutCheckHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    bool SendMediaSignal(const std::string& message);
    void ReadMediaSignalResponses();
    void PostMediaSignalResponse(std::string message);

    bool m_stop;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread work_thread;
    std::queue<std::shared_ptr<LogicNode>> m_msg_que;
    // UDS 读线程收到 MediaServer 的 offer/answer/candidate 后，重新投递到逻辑线程查找客户端连接。
    int m_media_fd = -1;
    std::mutex m_media_send_mutex;
    std::thread m_media_read_thread;
    std::unordered_map<std::uint64_t, std::weak_ptr<Session>> m_media_signal_sessions;
    std::uint64_t m_next_media_signal_id = 1;
    // LogicSystem 单线程访问，仅保存本进程中已进入会议的连接用于生命周期通知。
    std::unordered_map<std::uint64_t, std::vector<std::weak_ptr<Session>>> m_meeting_sessions;
    std::uint64_t m_next_message_sequence = 0;
};
