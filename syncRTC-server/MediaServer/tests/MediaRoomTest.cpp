#include "media/MediaPeer.h"
#include "media/MediaRoom.h"

#include <iostream>
#include <memory>

namespace {

bool Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    MediaRoom room(1001);
    auto peer = std::make_shared<MediaPeer>(1001, 20001);

    room.AddPeer(peer);
    if (!Expect(room.GetMeetingId() == 1001, "会议 ID 保存错误") ||
        !Expect(room.PeerCount() == 1, "成员数量保存错误") ||
        !Expect(room.GetPeer(20001) == peer, "成员查询错误")) {
        return 1;
    }

    room.RemovePeer(20001);
    if (!Expect(room.PeerCount() == 0, "成员移除失败") ||
        !Expect(!room.GetPeer(20001), "已移除成员仍可查询")) {
        return 1;
    }

    return 0;
}
