#pragma once

#include "common/Singleton.h"
#include "common/data.h"
#include "service/RoomThreadPool.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>

class MediaRoom;
class MediaSession;
class Session;

// RoomThreadPool 处理的媒体信令任务。Session 已经完成 UDS 长度帧解包，
// LogicSystem 完成 JSON 校验后，按 meeting_id 投递到固定 worker。
struct MediaSignalTask
{
    std::shared_ptr<Session> session;
    MediaSignalRequest request;
};

// 就绪通知只携带身份和媒体类型，不保活已离会的 MediaSession。
struct PublisherTrackReadyEvent
{
    std::uint64_t meeting_id;
    int publisher_uid;
    std::uint64_t session_id;
    std::string media_type;
};

// 库回调只提交数据和来源身份，UDS 连接使用弱引用，不延长断开连接的生命周期。
struct LocalMediaSignalEvent
{
    std::weak_ptr<Session> session;
    std::uint64_t session_id;
    MediaSignalResponse response;
};

// 保留旧连接对象用于比较身份；投递时其 I/O 已停止，不依赖可能被复用的 fd。
struct SignalDisconnectedEvent
{
    std::uint64_t meeting_id;
    std::shared_ptr<Session> session;
};

using MediaRoomEvent = std::variant<MediaSignalTask, PublisherTrackReadyEvent,
                                   LocalMediaSignalEvent, SignalDisconnectedEvent>;

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
    friend struct TrackReadyTestAccess;
    friend struct LocalSignalTestAccess;
public:
    ~LogicSystem();

    // 由该连接唯一的 UDS 读取线程调用：校验 JSON、登记房间，再投递到所属 worker。
    void PostMsgToQue(std::shared_ptr<Session> session, std::string message);
    // 仅在该连接读取循环结束、Close 完成后，由同一读取线程调用。
    void PostSignalDisconnected(const std::shared_ptr<Session>& session);

private:
    LogicSystem();

    void HandleRoomEvent(MediaRoomEvent event);
    void HandlePublisherTrackReady(const PublisherTrackReadyEvent& event);
    void HandleLocalMediaSignal(const LocalMediaSignalEvent& event);
    void HandleSignalDisconnected(const SignalDisconnectedEvent& event);
    void HandleSignal(const MediaSignalTask& task);
    void HandleOffer(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    void HandleAnswer(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    void HandleCandidate(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    void HandleLeave(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    // 仅在所属 RoomWorker 调用；实例为空时只回收空房间（例如会话构造失败）。
    void RemoveSession(const std::shared_ptr<MediaRoom>& room,
                       const std::shared_ptr<MediaSession>& session);

    // MediaServer 只保存当前进程内的运行时房间，会议成员资格仍由 RealtimeServer 管理。
    std::unordered_map<std::uint64_t, std::shared_ptr<MediaRoom>> m_rooms;
    // 不同 worker 会访问全局房间索引；锁只保护索引本身，房间业务不会持有该锁执行。
    std::mutex m_rooms_mutex;
    // 最后声明，析构时先排空 worker，再释放房间和索引锁。
    RoomThreadPool<MediaRoomEvent> m_room_workers;
};
