#pragma once

#include "common/Singleton.h"
#include "common/data.h"

#include <memory>
#include <unordered_map>

class MediaRoom;
class Session;

class LogicSystem : public Singleton<LogicSystem>, public std::enable_shared_from_this<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem() = default;

    void HandleOffer(std::shared_ptr<Session> session, const MediaSignalRequest& request);
    void HandleCandidate(std::shared_ptr<Session> session, const MediaSignalRequest& request);

private:
    LogicSystem() = default;

    // MediaServer 只保存当前进程内的运行时房间，会议成员资格仍由 RealtimeServer 管理。
    std::unordered_map<std::uint64_t, std::shared_ptr<MediaRoom>> m_rooms;
};
