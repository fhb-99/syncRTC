#include "common/data.h"
#include "storage/MySqlMgr.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {

template <typename T, typename = void>
struct HasHostUserId : std::false_type
{
};

template <typename T>
struct HasHostUserId<T, std::void_t<decltype(&T::host_user_id)>> : std::true_type
{
};

} // namespace

int main()
{
    static_assert(std::is_same_v<decltype(MeetingInfo::meeting_id), std::uint64_t>);
    static_assert(std::is_same_v<decltype(MeetingInfo::max_participants), std::uint16_t>);
    static_assert(!HasHostUserId<MeetingInfo>::value);
    static_assert(std::is_same_v<decltype(RecentMeetingInfo::creator_display_name),
                                 std::string>);
    static_assert(std::is_same_v<decltype(RecentMeetingInfo::creator_avatar_url),
                                 std::string>);
    static_assert(std::is_same_v<decltype(&MysqlMgr::GetMeetingRecently),
                                 bool (MysqlMgr::*)(int,
                                                   std::vector<RecentMeetingInfo>&)>);
    static_assert(std::is_same_v<decltype(&MysqlMgr::CreateMeeting),
                                 bool (MysqlMgr::*)(const CreateMeetingInfo&,
                                                   RecentMeetingInfo&)>);

    const MeetingInfo meeting;
    return meeting.meeting_id == 0 &&
                   meeting.status == MeetingStatus::kScheduled &&
                   meeting.visibility == MeetingVisibility::kPublic &&
                   meeting.max_participants == 30 &&
                   !meeting.scheduled_at.has_value() &&
                   !meeting.started_at.has_value() &&
                   !meeting.ended_at.has_value()
               ? 0
               : 1;
}
