#pragma once

#include "common/Singleton.h"
#include "common/global.h"
#include "net/Session.h"
#include "net/MediaSignalClient.h"
#include "common/meeting_ai.grpc.pb.h"

#include <grpcpp/grpcpp.h>

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
    // 仅 Media UDS 内部事件使用；客户端协议帧不携带此字段。
    std::uint64_t media_epoch = 0;
};

using callback = std::function<void(std::shared_ptr<Session> session,
                                    std::uint16_t id,
                                    std::string message)>;

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
    friend struct MediaSignalTestAccess;
public:
    ~LogicSystem();

    // Session 解出完整帧后，将同一类型的 LogicNode 投递到逻辑队列。
    void PostMsgToQue(std::shared_ptr<LogicNode> message);

    // CServer 发现 TCP 连接关闭后调用。断线清理仍在逻辑线程执行，避免 I/O 线程直接修改会议状态。
    void PostSessionDisconnected(std::shared_ptr<Session> session);
    // timerfd 每秒调用一次；超时清理统一进入逻辑线程执行。
    void PostReconnectTimeoutCheck();

    // 启动后台 UDS 连接维护；Media 暂不可用时继续启动 Realtime，并在后台重试。
    void StartMediaSignalClient(const std::string& socket_path);

private:
    LogicSystem();

    void DealMessage();

    void initHandlers();
    std::unordered_map<std::uint16_t, callback> maps;

    // 客户端登录处理
    void LoginHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    // AI 只处理会议内普通文本问答，不读取音频，也不携带会议上下文。
    void AiAskHandler(std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

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
    void PostMediaEvent(MediaSignalClient::Event event, std::uint64_t epoch, std::string message);
    bool AcceptMediaEvent(const LogicNode& message);

    bool m_stop;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread work_thread;
    std::queue<std::shared_ptr<LogicNode>> m_msg_que;
    // I/O 对象独占 socket；下列业务代次和信令映射只由逻辑线程维护。
    MediaSignalClient m_media_client;
    std::uint64_t m_media_epoch = 0;
    std::unordered_map<std::uint64_t, std::weak_ptr<Session>> m_media_signal_sessions;
    std::uint64_t m_next_media_signal_id = 1;
    // LogicSystem 单线程访问，仅保存本进程中已进入会议的连接用于生命周期通知。
    std::unordered_map<std::uint64_t, std::vector<std::weak_ptr<Session>>> m_meeting_sessions;
    std::uint64_t m_next_message_sequence = 0;
    // RealtimeServer 只保存到 AIServer 的 gRPC Stub，模型 Key 留在 AIServer。
    std::unique_ptr<message::MeetingAiService::Stub> m_ai_stub;
};
