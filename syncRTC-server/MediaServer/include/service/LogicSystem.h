#pragma once

#include "common/Singleton.h"
#include "common/data.h"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>

class MediaRoom;
class Session;

// MediaServer 逻辑线程的最小消息单元。Session 已经完成 UDS 长度帧解包，
// LogicSystem 再根据 JSON 中的 signal_type 分发 offer、answer 或 candidate。
struct MediaSignalNode
{
    std::shared_ptr<Session> session;
    std::string message;
};

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem();

    // I/O 线程只负责投递完整帧；队列按到达顺序由唯一逻辑线程处理。
    void PostMsgToQue(std::shared_ptr<MediaSignalNode> message);

private:
    LogicSystem();

    void DealMessage();
    void HandleSignal(std::shared_ptr<Session> session, std::string message);
    void HandleOffer(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    void HandleAnswer(const MediaSignalRequest& request);
    void HandleCandidate(std::shared_ptr<Session> session, const MediaSignalRequest& request);

    // MediaServer 只保存当前进程内的运行时房间，会议成员资格仍由 RealtimeServer 管理。
    std::unordered_map<std::uint64_t, std::shared_ptr<MediaRoom>> m_rooms;
    bool m_stop;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::thread m_work_thread;
    std::queue<std::shared_ptr<MediaSignalNode>> m_msg_que;
};
