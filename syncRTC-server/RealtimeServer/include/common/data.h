#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

struct UserInfo
{
    int uid;
    std::string username;
    std::string email;
};

// 与 meetings.status 对应的会议生命周期。数值必须与数据库中保存的状态保持一致。
enum class MeetingStatus : std::uint8_t
{
    kScheduled = 0,  // 已创建或已预约，但尚未开始。
    kInProgress = 1, // 会议正在进行，客户端可建立房间和媒体连接。
    kEnded = 2,      // 会议已正常结束，只能查看历史记录，不能再加入。
    kCancelled = 3,  // 会议被创建者或主持人取消，预约记录不再可用。
};

// 与 meetings.visibility 对应的访问范围；会议密码是否存在由 requires_password 单独表达。
enum class MeetingVisibility : std::uint8_t
{
    kPublic = 0,  // 知道会议号的用户可申请加入，仍可能需要验证会议密码。
    kPrivate = 1, // 仅创建者、主持人或已获准的参与者可以加入。
};

using MeetingTimePoint = std::chrono::system_clock::time_point;

// MySQL meetings 表的一条会议基础信息。
// 此结构体不保存 Redis 中的实时成员、实时人数或设备状态，也绝不保存会议密码哈希。
struct MeetingInfo
{
    // meetings.id：数据库主键，仅在服务端和内部接口中标识一场会议。
    std::uint64_t meeting_id = 0;

    // meetings.meeting_code：面向用户展示、复制和输入的唯一会议号；不是密码，也不是会话 token。
    std::string meeting_code;

    // meetings.title：会议主题，展示在首页待参加会议、会议详情和历史会议列表中。
    std::string title;

    // meetings.creator_user_id：创建这场会议的用户 ID，用于审计和创建者权限判断。
    std::uint64_t creator_user_id = 0;

    // meetings.host_user_id：当前主持人用户 ID，用于控制结束会议、成员管理和设备管理等主持权限。
    std::uint64_t host_user_id = 0;

    // meetings.status：会议所处的持久化生命周期，不承载 Redis 中瞬时的成员在线变化。
    MeetingStatus status = MeetingStatus::kScheduled;

    // meetings.visibility：会议访问范围，决定普通用户能否仅凭会议号请求加入。
    MeetingVisibility visibility = MeetingVisibility::kPublic;

    // 由 meetings.meeting_password_hash 是否为空派生；只告诉客户端是否需要输入密码，绝不下发哈希值。
    bool requires_password = false;

    // meetings.max_participants：允许同时处于会议房间内的最大人数，实时人数由 Redis 原子维护。
    std::uint16_t max_participants = 30;

    // meetings.scheduled_at：预约会议的计划开始时间；立即开始的会议可为空。
    std::optional<MeetingTimePoint> scheduled_at;

    // meetings.started_at：会议实际开始时间，用于首页状态、会议时长和历史记录展示。
    std::optional<MeetingTimePoint> started_at;

    // meetings.ended_at：会议实际结束时间；为空说明会议尚未正常结束或尚未开始。
    std::optional<MeetingTimePoint> ended_at;

    // meetings.created_at：会议记录创建时间，用于审计和“最近创建”排序。
    MeetingTimePoint created_at{};

    // meetings.updated_at：会议基础信息最后一次持久化修改时间，用于缓存失效和并发更新判断。
    MeetingTimePoint updated_at{};
};



struct RecentMeetingInfo
{
    std::string meeting_code;       // 用户复制或输入的会议号
    std::string title;              // 会议标题
    std::string host_display_name; // 主持人名称
    std::string host_avatar_url;   // 主持人头像
    MeetingStatus status;          // 未开始、进行中、已结束等
    bool requires_password;        // 是否需要输入会议密码
    std::uint16_t max_participants;// 人数上限
    std::string scheduled_at;      // 格式化后的预约开始时间
};