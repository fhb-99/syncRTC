#include "service/LogicSystem.h"
#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"
#include "net/Session.h"

#include <json/json.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <unistd.h>

namespace {

void Expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
void WaitUntil(Predicate predicate, const char* message)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!predicate()) {
        Expect(std::chrono::steady_clock::now() < deadline, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

// 测试通过真实 RoomWorker 入队，直接观察房间发布表，不为生产代码增加查询或等待接口。
// 手动构造的 Peer 只在上一组事件已完成后安装，用于精确安排离会和旧回调的先后顺序。
struct TrackReadyTestAccess
{
    inline static std::shared_ptr<Session> connection;
    static std::shared_ptr<MediaRoom> FindRoom(LogicSystem& logic, std::uint64_t meeting_id)
    {
        std::lock_guard<std::mutex> lock(logic.m_rooms_mutex);
        const auto it = logic.m_rooms.find(meeting_id);
        return it == logic.m_rooms.end() ? nullptr : it->second;
    }

    static bool HasPublisher(const std::shared_ptr<MediaRoom>& room, int uid,
                             const std::string& media_type)
    {
        if (!room) {
            return false;
        }
        std::lock_guard<std::mutex> lock(room->m_mutex);
        const auto& publishers = media_type == "video" ? room->m_video_publishers
                                                       : room->m_audio_publishers;
        return publishers.count(uid) != 0;
    }

    static std::shared_ptr<MediaSession> AddPeer(const std::shared_ptr<MediaRoom>& room, int uid)
    {
        auto session = std::make_shared<MediaSession>(room->GetMeetingId(), uid, 1, connection,
            [](std::uint64_t, MediaSignalResponse) {},
            [](int, std::uint64_t, const std::string&) {},
            [](int, std::uint64_t, const std::string&, rtc::binary) {});
        auto peer = std::make_shared<MediaPeer>(room->GetMeetingId(), uid);
        peer->SetSession(session);
        room->AddPeer(peer);
        return session;
    }

    static void PostReady(LogicSystem& logic, std::uint64_t meeting_id, int uid,
                          std::uint64_t session_id, const std::string& media_type)
    {
        Expect(logic.m_room_workers.Post(meeting_id,
            PublisherTrackReadyEvent{meeting_id, uid, session_id, media_type}), "就绪事件入队失败");
    }

    static void PostLeave(LogicSystem& logic, std::uint64_t meeting_id, int uid)
    {
        Json::Value request;
        request["signal_id"] = Json::UInt64(1);
        request["meeting_id"] = Json::UInt64(meeting_id);
        request["uid"] = uid;
        request["signal_type"] = "leave";
        logic.PostMsgToQue(connection, request.toStyledString());
    }

    static void Run()
    {
        int sockets[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "创建测试信令连接失败");
        connection = std::make_shared<Session>(sockets[0]);
        const auto logic = LogicSystem::GetInstance();
        constexpr std::uint64_t meeting_id = 1001;
        constexpr int uid = 20001;

        // 用真实 libdatachannel Offer 走生产入口，验证 SetRemoteOffer 的通知确实进入
        // LogicSystem 的新事件分支并完成音视频发布登记，不只测试手工构造的事件。
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        config.disableAutoGathering = true;
        rtc::PeerConnection publisher(config);
        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        video.addSSRC(1234, "video", "test", "video");
        auto video_track = publisher.addTrack(video);
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        audio.addSSRC(1235, "audio", "test", "audio");
        auto audio_track = publisher.addTrack(audio);
        publisher.setLocalDescription();
        const auto offer = publisher.localDescription();
        Expect(offer.has_value(), "测试发布者未生成 Offer");

        Json::Value request;
        request["signal_id"] = Json::UInt64(1);
        request["meeting_id"] = Json::UInt64(meeting_id);
        request["uid"] = uid;
        request["signal_type"] = "offer";
        request["sdp"] = std::string(*offer);
        logic->PostMsgToQue(connection, request.toStyledString());
        WaitUntil([&]() {
            const auto room = FindRoom(*logic, meeting_id);
            return HasPublisher(room, uid, "video") && HasPublisher(room, uid, "audio");
        }, "真实 Offer 的 Track 就绪通知未完成发布登记");

        const auto room = FindRoom(*logic, meeting_id);
        const auto old_session = room->GetPeer(uid)->GetSession();
        const auto old_id = old_session->GetInstanceId();

        // 第二个成员使 room 在发布者离会后仍存活。其 video 通知作为同 room FIFO
        // 的完成标记：观察到该登记时，前面的 Leave 和旧通知都已经处理完毕。
        const auto other_session = AddPeer(room, uid + 1);
        PostLeave(*logic, meeting_id, uid);
        PostReady(*logic, meeting_id, uid, old_id, "video");
        PostReady(*logic, meeting_id, uid, old_id, "audio");
        PostReady(*logic, meeting_id, uid + 1, other_session->GetInstanceId(), "video");
        WaitUntil([&]() { return HasPublisher(room, uid + 1, "video"); }, "离会测试队列未排空");
        Expect(!room->GetPeer(uid), "离会后旧 Peer 仍存在");
        Expect(!HasPublisher(room, uid, "video") && !HasPublisher(room, uid, "audio"),
               "离会后的旧通知重新登记了发布者");

        // 保持旧对象存活，证明拒绝依据是会话身份，而不只是 weak_ptr 是否过期。
        // 同 UID 重入时先投旧 video，再投新 audio；后者完成后旧 video 必须仍不存在。
        const auto new_session = AddPeer(room, uid);
        const auto new_id = new_session->GetInstanceId();
        Expect(old_id != new_id, "同 UID 的新旧会话实例号相同");
        PostReady(*logic, meeting_id, uid, old_id, "video");
        PostReady(*logic, meeting_id, uid, new_id, "audio");
        WaitUntil([&]() { return HasPublisher(room, uid, "audio"); }, "新会话 audio 通知未执行");
        Expect(!HasPublisher(room, uid, "video"), "旧会话通知污染了同 UID 的新会话");
        PostReady(*logic, meeting_id, uid, new_id, "video");
        WaitUntil([&]() { return HasPublisher(room, uid, "video"); }, "新会话 video 通知被错误拒绝");

        // 通过真实 Leave 删除整个 room，再模拟删除后到达的旧通知。
        PostLeave(*logic, meeting_id, uid);
        PostLeave(*logic, meeting_id, uid + 1);
        WaitUntil([&]() { return !FindRoom(*logic, meeting_id); }, "最后一个成员离开后 room 未删除");
        // 该通知执行时可能已经重建 room，两种情况下都不能登记旧会话。
        PostReady(*logic, meeting_id, uid, new_id, "video");

        // 重用会议号并新建会话。旧 room 的 shared_ptr 仍在测试中保活，不能作为有效性依据。
        const auto recreated_room = std::make_shared<MediaRoom>(meeting_id);
        const auto recreated_session = AddPeer(recreated_room, uid);
        {
            std::lock_guard<std::mutex> lock(logic->m_rooms_mutex);
            logic->m_rooms.emplace(meeting_id, recreated_room);
        }
        PostReady(*logic, meeting_id, uid, old_id, "video");
        PostReady(*logic, meeting_id, uid, new_id, "video");
        PostReady(*logic, meeting_id, uid, recreated_session->GetInstanceId(), "audio");
        WaitUntil([&]() { return HasPublisher(recreated_room, uid, "audio"); }, "重建房间通知未执行");
        Expect(!HasPublisher(recreated_room, uid, "video"), "旧房间通知污染了重建房间");
        PostLeave(*logic, meeting_id, uid);
        WaitUntil([&]() { return !FindRoom(*logic, meeting_id); }, "测试结束清理房间失败");
        publisher.close();
        connection->Close();
        connection.reset();
        ::close(sockets[1]);
    }
};

int main()
{
    try {
        TrackReadyTestAccess::Run();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
