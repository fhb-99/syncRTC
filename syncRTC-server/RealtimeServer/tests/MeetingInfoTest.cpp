#include "common/data.h"
#include "storage/MySqlMgr.h"

#include <cstdint>
#include <type_traits>
#include <vector>

int main()
{
    static_assert(std::is_same_v<decltype(MeetingInfo::meeting_id), std::uint64_t>);
    static_assert(std::is_same_v<decltype(MeetingInfo::max_participants), std::uint16_t>);
    static_assert(std::is_same_v<decltype(&MysqlMgr::GetMeetingRecently),
                                 bool (MysqlMgr::*)(int,
                                                   std::vector<RecentMeetingInfo>&)>);

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
