#pragma once

#include "common/Singleton.h"
#include "common/global.h"
#include "net/Session.h"
#include "net/MediaSignalClient.h"
#include "common/meeting_ai.grpc.pb.h"
#include "service/BusinessScheduler.h"

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

// 业务入口处理的消息单元，保存来源连接和已解出的完整协议帧。
struct LogicNode
{
    std::shared_ptr<Session> session;
    std::uint16_t id;
    std::string message;
    // 仅 Media UDS 内部事件使用；客户端协议帧不携带此字段。
    std::uint64_t media_epoch = 0;
};

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
    friend struct MediaSignalTestAccess;
public:
    ~LogicSystem();

    // 停止 I/O 后显式排空业务，必须早于数据库等单例的静态析构。
    void Shutdown();

    // Session 解帧后投递连接入口；归属解析完成后进入对应会议的串行命令队列。
    void PostMsgToQue(std::shared_ptr<LogicNode> message);

    // CServer 发现关闭后投递最后一条连接命令，随后在原会议串行清理。
    void PostSessionDisconnected(std::shared_ptr<Session> session);
    // timerfd 每秒调用；发现到期候选后分发到各会议重新核验和清理。
    void PostReconnectTimeoutCheck();

    // 交给 CServer 的唯一 I/O 循环驱动；业务侧仍只通过 Send / IsCurrent 访问。
    MediaSignalClient& GetMediaSignalClient();

private:
    LogicSystem();

    using Operation = SerialCommandQueue::Operation;
    struct Membership {
        std::string ticket;
        std::string auth_token;
        std::string last_request;
        std::uint64_t generation = 1;
        std::weak_ptr<Session> session;
        std::chrono::steady_clock::time_point deadline{};
    };
    struct Room {
        explicit Room(BusinessScheduler& scheduler, std::uint64_t value)
            : id(value), queue(std::make_shared<SerialCommandQueue>(scheduler)) {}
        const std::uint64_t id;
        std::shared_ptr<SerialCommandQueue> queue;
        // 票据只在当前 Realtime 进程有效；断线宽限期间也保留 Room。
        std::unordered_map<int, Membership> members;
        std::vector<std::weak_ptr<Session>> sessions; // 只由本会议的串行操作访问。
    };
    using callback = std::function<void(Operation&, Room&, std::shared_ptr<Session>, std::uint16_t, std::string)>;
    void Route(Operation& op, const std::shared_ptr<LogicNode>& message);
    void Dispatch(Operation& op, Room& room, const std::shared_ptr<LogicNode>& message);
    bool SubmitRoom(std::uint64_t id, std::function<void(Operation&, Room&)> command,
                    std::function<void()> done = {}, bool terminal = false, std::size_t bytes = 0);
    void ReplyError(const std::shared_ptr<LogicNode>& message, int error);
    void ExpireMemberHandler(Operation& op, Room& room, const std::string& member);
    bool ReserveUser(int uid, std::uint64_t meeting);
    void FinishJoin(int uid, std::uint64_t meeting, bool joined);
    void ReleaseUser(int uid, std::uint64_t meeting);
    void ReleaseRoomUsers(std::uint64_t meeting);
    struct Signal {
        std::weak_ptr<Session> session;
        std::uint64_t meeting = 0;
        std::uint64_t epoch = 0;
    };
    std::uint64_t RegisterSignal(std::uint64_t id, const std::shared_ptr<Session>& session, std::uint64_t meeting);
    Signal FindSignal(std::uint64_t id);
    void RemoveSignal(std::uint64_t id);
    void ReleaseMediaSession(const std::shared_ptr<Session>& session);


    void initHandlers();
    std::unordered_map<std::uint16_t, callback> maps;

    // 客户端登录处理
    void LoginHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    // AI 只处理会议内普通文本问答，不读取音频，也不携带会议上下文。
    void AiAskHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 客户端创建会议处理
    void CreateMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 得到客户端历史会议信息处理
    void GetPastMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    // 处理用户的入会请求
    void JoinMeeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void StartMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void LeaveMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void EndMeetingHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void SendMeetingMessageHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void GetMeetingGroupMessagesHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void GetMeetingPrivateMessagesHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    // 媒体信令处理先预留接口，后续再接入 MediaServer 转发链路。
    void MediaOfferHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaAnswerHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaCandidateHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void MediaSignalResponseHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void SessionDisconnectedHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);
    void ReconnectTimeoutCheckHandler(Operation& op, Room& room, std::shared_ptr<Session> session, std::uint16_t&, std::string& message);

    bool SendMediaSignal(const std::string& message, std::uint64_t epoch);
    void PostMediaEvent(MediaSignalClient::Event event, std::uint64_t epoch, std::string message);
    bool AcceptMediaEvent(const LogicNode& message);

    BusinessScheduler m_scheduler{4, 2048};
    SlowPool m_storage_pool{4, 128};
    SlowPool m_ai_pool{2, 32};
    std::shared_ptr<SerialCommandQueue> m_control;
    std::shared_ptr<SerialCommandQueue> m_timer_queue;
    std::mutex m_registry_mutex;
    bool m_stopping = false;
    std::unordered_map<Session*, std::shared_ptr<SerialCommandQueue>> m_connections;
    std::unordered_map<std::uint64_t, std::shared_ptr<Room>> m_rooms;
    struct UserPresence { std::uint64_t meeting; std::size_t joining = 0; bool joined = false; };
    std::mutex m_user_mutex;
    std::unordered_map<int, UserPresence> m_user_meetings;
    MediaSignalClient m_media_client;
    std::mutex m_signal_mutex;
    std::atomic<std::uint64_t> m_media_epoch{0};
    std::unordered_map<std::uint64_t, Signal> m_signals;
    std::atomic<std::uint64_t> m_next_media_signal_id{1};
    // RealtimeServer 只保存到 AIServer 的 gRPC Stub，模型 Key 留在 AIServer。
    std::unique_ptr<message::MeetingAiService::Stub> m_ai_stub;
};
