#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"
#include "net/Session.h"
#include "service/LogicSystem.h"

#include <json/json.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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

// 使用真实 UDS 发送队列观察最终响应，不替换 Session::Send 或 RoomWorker。
struct Connection
{
    Connection()
    {
        int sockets[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "创建 UDS 失败");
        session = std::make_shared<Session>(sockets[0]);
        peer_fd = sockets[1];
        timeval timeout{5, 0};
        Expect(::setsockopt(peer_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0,
               "设置读取超时失败");
    }

    ~Connection()
    {
        session->Close();
        ::close(peer_fd);
    }

    void ReadAll(char* data, std::size_t length)
    {
        while (length != 0) {
            const ssize_t count = ::recv(peer_fd, data, length, 0);
            Expect(count > 0, "等待本地信令响应超时或连接关闭");
            data += count;
            length -= static_cast<std::size_t>(count);
        }
    }

    Json::Value Read()
    {
        std::array<unsigned char, 4> header{};
        ReadAll(reinterpret_cast<char*>(header.data()), header.size());
        const std::uint32_t length = (static_cast<std::uint32_t>(header[0]) << 24U) |
            (static_cast<std::uint32_t>(header[1]) << 16U) |
            (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
        Expect(length < 1024 * 1024, "本地信令帧长度异常");
        std::string payload(length, '\0');
        ReadAll(payload.data(), payload.size());
        Json::Value response;
        Json::Reader reader;
        Expect(reader.parse(payload, response), "本地信令 JSON 无效");
        return response;
    }

    std::shared_ptr<Session> session;
    int peer_fd = -1;
};

// 真实的发布端 SDP；本测试只验证信令和会话生命周期，不建立媒体网络连接。
struct Publisher
{
    Publisher()
    {
        rtc::Configuration config;
        config.disableAutoNegotiation = true;
        config.disableAutoGathering = true;
        pc = std::make_shared<rtc::PeerConnection>(config);
        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        video.addSSRC(1234, "video", "test", "video");
        tracks.push_back(pc->addTrack(video));
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        audio.addSSRC(1235, "audio", "test", "audio");
        tracks.push_back(pc->addTrack(audio));
        pc->setLocalDescription();
        offer = std::string(*pc->localDescription());
    }

    std::shared_ptr<rtc::PeerConnection> pc;
    std::vector<std::shared_ptr<rtc::Track>> tracks;
    std::string offer;
};

// 在真实 Worker 的 Track 就绪通知中暂停，确定性安排在途 Offer、排队事件和断开顺序。
struct WorkerGate
{
    ~WorkerGate() { Release(); }

    void Block()
    {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        Expect(condition.wait_for(lock, std::chrono::seconds(5), [&]() { return released; }),
               "等待测试释放 Worker 超时");
    }

    void WaitEntered()
    {
        std::unique_lock<std::mutex> lock(mutex);
        Expect(condition.wait_for(lock, std::chrono::seconds(5), [&]() { return entered; }),
               "Worker 未进入在途 Offer");
    }

    void Release()
    {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }

    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;
};

} // namespace

struct LocalSignalTestAccess
{
    static constexpr std::uint64_t kMeetingId = 9001;
    static constexpr int kUid = 20001;

    static std::shared_ptr<MediaRoom> FindRoom(LogicSystem& logic, std::uint64_t meeting_id = kMeetingId)
    {
        std::lock_guard<std::mutex> lock(logic.m_rooms_mutex);
        const auto room_it = logic.m_rooms.find(meeting_id);
        return room_it == logic.m_rooms.end() ? nullptr : room_it->second;
    }

    static std::shared_ptr<MediaSession> Current(LogicSystem& logic, int uid = kUid)
    {
        const auto room = FindRoom(logic);
        const auto peer = room ? room->GetPeer(uid) : nullptr;
        return peer ? peer->GetSession() : nullptr;
    }

    static void PostRequest(LogicSystem& logic, Connection& connection, std::uint64_t signal_id,
                            const std::string& type, const std::string& sdp = "", int uid = kUid,
                            std::uint64_t meeting_id = kMeetingId)
    {
        Json::Value request;
        request["meeting_id"] = Json::UInt64(meeting_id);
        request["uid"] = uid;
        request["signal_id"] = Json::UInt64(signal_id);
        request["signal_type"] = type;
        request["sdp"] = sdp;
        request["candidate"] = "invalid-old-candidate";
        request["mid"] = "video";
        logic.PostMsgToQue(connection.session, request.toStyledString());
    }

    static void PostLocal(LogicSystem& logic, const std::weak_ptr<Session>& connection,
                          std::uint64_t instance_id, std::uint64_t signal_id,
                          const std::string& type, const std::string& payload)
    {
        MediaSignalResponse response;
        response.meeting_id = kMeetingId;
        response.uid = kUid;
        response.signal_id = signal_id;
        response.signal_type = type;
        response.sdp = payload;
        response.candidate = payload;
        response.mid = "video";
        Expect(logic.m_room_workers.Post(kMeetingId,
            LocalMediaSignalEvent{connection, instance_id, std::move(response)}), "本地事件入队失败");
    }

    static void WaitAnswerAndCandidate(Connection& connection, std::uint64_t signal_id,
                                       bool reused_signal_id = false)
    {
        bool answer = false;
        bool candidate = false;
        while (!answer || !candidate) {
            const auto response = connection.Read();
            Expect(response["sdp"].asString() != "stale" &&
                   response["candidate"].asString() != "stale", "旧回调被发往 UDS");
            // 替换前已经进入 UDS 队列的合法响应不能撤回；这里只等待新编号的响应。
            if (response["uid"].asInt() != kUid || response["signal_id"].asUInt64() != signal_id) {
                continue;
            }
            const auto type = response["signal_type"].asString();
            if (type == "answer") {
                Expect(!response["sdp"].asString().empty(), "Answer SDP 为空");
                answer = true;
            }
            else if (type == "candidate") {
                if (reused_signal_id && !answer) {
                    continue;
                }
                Expect(answer && !response["candidate"].asString().empty(), "Candidate 先于 Answer 或内容为空");
                candidate = true;
            }
        }
    }

    // Marker 与旧事件经过同一个 Room FIFO 和 UDS FIFO，读到它即可确认前面的事件已经执行。
    static void WaitMarker(Connection& connection, const std::string& marker)
    {
        while (true) {
            const auto response = connection.Read();
            Expect(response["sdp"].asString() != "stale" &&
                   response["candidate"].asString() != "stale", "旧会话或错误 signal_id 的响应被发出");
            if (response["candidate"].asString() == marker) {
                return;
            }
        }
    }

    static void TestDispatchAndReplacement()
    {
        const auto logic = LogicSystem::GetInstance();
        Connection connection;
        Publisher publisher;
        PostRequest(*logic, connection, 101, "offer", publisher.offer);
        WaitAnswerAndCandidate(connection, 101);
        const auto original = Current(*logic);
        Expect(static_cast<bool>(original), "初始媒体会话不存在");

        // 第二个成员触发真实服务端 Offer。每个会话沿用自己的 signal_id，且新成员先收到 Answer。
        Publisher second;
        PostRequest(*logic, connection, 102, "offer", second.offer, kUid + 1);
        bool second_answer = false;
        bool second_offer = false;
        bool first_offer = false;
        while (!second_offer || !first_offer) {
            const auto response = connection.Read();
            const auto type = response["signal_type"].asString();
            if (response["uid"].asInt() == kUid + 1) {
                Expect(response["signal_id"].asUInt64() == 102, "新成员的服务端 Offer 编号错误");
                second_answer = second_answer || type == "answer";
                if (type == "offer") {
                    Expect(second_answer, "服务端 Offer 先于初始 Answer");
                    second_offer = true;
                }
            }
            else {
                Expect(response["signal_id"].asUInt64() == 101, "原成员的服务端 Offer 未沿用原编号");
                first_offer = first_offer || type == "offer";
            }
        }
        PostRequest(*logic, connection, 102, "leave", "", kUid + 1);
        PostLocal(*logic, connection.session, original->GetInstanceId(), 101, "candidate", "before-replace");
        WaitMarker(connection, "before-replace");

        // 新客户端初始 Offer 替换同 UID 会话。旧对象保持存活，以验证实例校验而不是弱引用过期。
        Publisher replacement;
        PostRequest(*logic, connection, 103, "offer", replacement.offer);
        const auto old_id = original->GetInstanceId();
        PostLocal(*logic, connection.session, old_id, 101, "answer", "stale");
        PostLocal(*logic, connection.session, old_id, 101, "candidate", "stale");
        WaitAnswerAndCandidate(connection, 103);
        const auto current = Current(*logic);
        Expect(current && current->GetInstanceId() != old_id, "新编号 Offer 未替换旧会话");

        // 错误编号的回调和旧远端信令都必须被忽略，不能修改当前会话。
        PostLocal(*logic, connection.session, current->GetInstanceId(), 101, "offer", "stale");
        PostRequest(*logic, connection, 101, "answer", "invalid-old-sdp");
        PostRequest(*logic, connection, 101, "candidate");
        PostRequest(*logic, connection, 101, "leave");
        PostLocal(*logic, {}, current->GetInstanceId(), 103, "candidate", "stale");
        PostLocal(*logic, connection.session, current->GetInstanceId(), 103, "candidate", "current");
        WaitMarker(connection, "current");
        Expect(Current(*logic) == current, "旧远端信令影响了新会话");

        PostRequest(*logic, connection, 103, "leave");
        PostLocal(*logic, connection.session, current->GetInstanceId(), 103, "candidate", "stale");
        // 重用会议号、UID 甚至 signal_id，仍应通过不同的会话实例号隔离旧事件。
        Publisher recreated;
        PostRequest(*logic, connection, 103, "offer", recreated.offer);
        PostLocal(*logic, connection.session, current->GetInstanceId(), 103, "answer", "stale");
        WaitAnswerAndCandidate(connection, 103, true);
        const auto rebuilt = Current(*logic);
        Expect(rebuilt && rebuilt->GetInstanceId() != current->GetInstanceId(), "重建房间复用了旧会话");
        PostLocal(*logic, connection.session, rebuilt->GetInstanceId(), 103, "candidate", "rebuilt");
        WaitMarker(connection, "rebuilt");
        PostRequest(*logic, connection, 103, "leave");
        WaitUntil([&]() { return !Current(*logic); }, "清理测试房间失败");
    }

    static void TestCallbacksAfterDestruction()
    {
        struct Results
        {
            std::mutex mutex;
            std::condition_variable condition;
            int answers = 0;
            bool wrong_identity = false;
        };
        const auto results = std::make_shared<Results>();
        Publisher publisher;
        auto media = std::make_shared<MediaSession>(9002, kUid, 501, std::weak_ptr<Session>{},
            [results](std::uint64_t, MediaSignalResponse response) {
                std::lock_guard<std::mutex> lock(results->mutex);
                results->wrong_identity = results->wrong_identity || response.signal_id != 501;
                if (response.signal_type == "answer") {
                    ++results->answers;
                }
                results->condition.notify_one();
            },
            [](int, std::uint64_t, const std::string&) {},
            [](int, std::uint64_t, const std::string&, rtc::binary) {});
        media->SetRemoteOffer(publisher.offer);
        const auto pc = media->m_peer_connection;
        const std::weak_ptr<MediaSession> weak_media = media;
        media.reset();
        WaitUntil([&]() { return weak_media.expired(); }, "回调循环引用导致会话无法析构");

        // 故意保留库对象并在业务对象析构后生成新 Track 和本地 SDP。
        // onTrack 的弱引用应失效，SDP/ICE 回调只访问按值捕获的数据，不依赖裸 this。
        rtc::Description offer(publisher.offer, "offer");
        rtc::Description::Video extra("extra", rtc::Description::Direction::SendOnly);
        extra.addH264Codec(96);
        extra.addSSRC(1236, "extra", "test", "extra");
        offer.addMedia(extra);
        pc->setRemoteDescription(offer);
        pc->setLocalDescription();
        std::unique_lock<std::mutex> lock(results->mutex);
        Expect(results->condition.wait_for(lock, std::chrono::seconds(5), [&]() {
            return results->answers >= 2;
        }), "会话析构后库回调未完成");
        Expect(!results->wrong_identity, "析构后的回调身份变化");
        lock.unlock();
        pc->close();
    }

    static void TestControlErrors()
    {
        const auto logic = LogicSystem::GetInstance();
        Connection connection;
        Publisher publisher;
        const std::uint64_t other_room = kMeetingId + 4;
        Expect(logic->m_room_workers.WorkerIndex(kMeetingId) ==
               logic->m_room_workers.WorkerIndex(other_room), "测试房间不在同一 Worker");

        // 无效 Offer 经过生产入口和真实工作线程。随后同 Worker 的另一房间仍须完成协商。
        PostRequest(*logic, connection, 601, "offer", "invalid-sdp");
        PostRequest(*logic, connection, 602, "offer", publisher.offer, kUid, other_room);
        WaitAnswerAndCandidate(connection, 602);
        Expect(!FindRoom(*logic), "无效 Offer 留下了空房间或半初始化会话");

        PostRequest(*logic, connection, 603, "offer", publisher.offer);
        WaitAnswerAndCandidate(connection, 603);
        const auto original = Current(*logic);
        Expect(static_cast<bool>(original), "有效 Offer 未建立会话");

        // 新编号的无效 Offer 会替换旧会话；失败后同 UID 可以再次入会。
        PostRequest(*logic, connection, 604, "offer", "invalid-sdp");
        PostRequest(*logic, connection, 605, "offer", publisher.offer);
        WaitAnswerAndCandidate(connection, 605);
        const auto replacement = Current(*logic);
        Expect(replacement && replacement->GetInstanceId() != original->GetInstanceId(),
               "失败后同 UID 无法重建会话");
        PostRequest(*logic, connection, 603, "answer", "invalid-old-sdp");
        PostRequest(*logic, connection, 603, "candidate");
        PostLocal(*logic, connection.session, original->GetInstanceId(), 603, "answer", "stale");
        PostLocal(*logic, connection.session, replacement->GetInstanceId(), 605, "candidate", "after-error");
        WaitMarker(connection, "after-error");
        Expect(Current(*logic) == replacement, "旧信令或回调误清理了同 UID 新会话");

        // 第二个成员触发真正的重新协商，再用无效 Answer 验证清理消费 Track，保留其他成员。
        PostRequest(*logic, connection, 606, "offer", publisher.offer, kUid + 1);
        bool requested_answer = false;
        bool second_offer = false;
        while (!requested_answer || !second_offer) {
            const auto response = connection.Read();
            if (response["signal_type"].asString() == "offer") {
                requested_answer = requested_answer || response["signal_id"].asUInt64() == 605;
                second_offer = second_offer || response["signal_id"].asUInt64() == 606;
            }
        }
        const auto survivor = Current(*logic, kUid + 1);
        PostRequest(*logic, connection, 605, "answer", "invalid-answer");
        // 同房间标记必须排在失败清理后，不能只通过异步读取成员表判断任务完成。
        MediaSignalResponse marker;
        marker.meeting_id = kMeetingId;
        marker.uid = kUid + 1;
        marker.signal_id = 606;
        marker.signal_type = "candidate";
        marker.candidate = "answer-cleaned";
        logic->m_room_workers.Post(kMeetingId,
            LocalMediaSignalEvent{connection.session, survivor->GetInstanceId(), marker});
        WaitMarker(connection, "answer-cleaned");
        Expect(!Current(*logic) && Current(*logic, kUid + 1) == survivor,
               "无效 Answer 未清理失败会话或误清理其他成员");
        {
            std::lock_guard<std::mutex> lock(survivor->m_track_mutex);
            Expect(survivor->m_outgoing_video_tracks.count(kUid) == 0 &&
                   survivor->m_outgoing_audio_tracks.count(kUid) == 0,
                   "其他成员中残留失败发布者的消费 Track");
        }

        // Candidate 格式错误同样由库抛出异常，最后一个成员失败后须回收房间。
        PostRequest(*logic, connection, 606, "candidate", "", kUid + 1);
        WaitUntil([&]() { return !FindRoom(*logic); }, "无效 Candidate 未清理会话和空房间");
        Expect(static_cast<bool>(FindRoom(*logic, other_room)), "失败影响了另一个房间");
        PostRequest(*logic, connection, 602, "leave", "", kUid, other_room);
        WaitUntil([&]() { return !FindRoom(*logic, other_room); }, "其他房间无法继续处理 Leave");
    }

    static void TestCleanupIdentityAndTrackFailure()
    {
        Connection connection;
        // 此处不投递任务，独立 LogicSystem 的 Worker 保持空闲，便于直接验证清理边界。
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        auto room = std::make_shared<MediaRoom>(kMeetingId);
        logic->m_rooms.emplace(kMeetingId, room);
        const auto add_peer = [&](int uid) {
            auto media = std::make_shared<MediaSession>(kMeetingId, uid, 701, connection.session,
                [](std::uint64_t, MediaSignalResponse) {},
                [](int, std::uint64_t, const std::string&) {},
                [](int, std::uint64_t, const std::string&, rtc::binary) {});
            auto peer = std::make_shared<MediaPeer>(kMeetingId, uid);
            peer->SetSession(media);
            room->AddPeer(peer);
            return media;
        };
        const auto old = add_peer(kUid);
        room->RemovePeer(kUid);
        const auto current = add_peer(kUid);
        const auto subscriber = add_peer(kUid + 1);
        subscriber->AddOutgoingTrack(kUid, current->GetInstanceId(), "video", 1234);
        // 新旧对象连 signal_id 都相同；旧对象清理不能删除新成员或新消费 Track。
        logic->RemoveSession(room, old);
        Expect(room->GetPeer(kUid)->GetSession() == current, "旧实例清理删除了新会话");
        Expect(subscriber->m_outgoing_video_tracks.at(kUid).publisher_session_id == current->GetInstanceId(),
               "旧实例清理删除了新会话的消费 Track");

        struct RejectTrack : rtc::MediaHandler
        {
            void media(const rtc::Description::Media&) override
            {
                throw std::runtime_error("test: reject track");
            }
        };
        subscriber->RemoveOutgoingTracks(kUid);
        subscriber->m_peer_connection->setMediaHandler(std::make_shared<RejectTrack>());
        const auto healthy = add_peer(kUid + 2);
        // 通过库的 MediaHandler 注入 addTrack 失败，验证清理的是接收者而非通知发布者。
        logic->HandleRoomEvent(PublisherTrackReadyEvent{kMeetingId, kUid, current->GetInstanceId(), "video"});
        logic->HandleRoomEvent(PublisherTrackReadyEvent{kMeetingId, kUid, current->GetInstanceId(), "audio"});
        Expect(!room->GetPeer(kUid + 1), "添加消费 Track 失败的接收会话未清理");
        Expect(room->GetPeer(kUid)->GetSession() == current && room->GetPeer(kUid + 2)->GetSession() == healthy,
               "接收者失败误清理了发布者或健康成员");
        Expect(healthy->m_outgoing_video_tracks.count(kUid) == 1 &&
               healthy->m_outgoing_audio_tracks.count(kUid) == 1,
               "接收者失败中断了其他成员的 Track 创建");

        logic->RemoveSession(room, current);
        logic->RemoveSession(room, healthy);
        auto rebuilt = std::make_shared<MediaRoom>(kMeetingId);
        logic->m_rooms.emplace(kMeetingId, rebuilt);
        logic->RemoveSession(room, old);
        Expect(FindRoom(*logic) == rebuilt, "旧空房间清理误删了重建房间");
    }

    static void TestDisconnectWithQueuedSignals()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        Connection old_connection;
        Connection new_connection;
        Connection unrelated_connection;
        Publisher publisher;
        auto gate = std::make_shared<WorkerGate>();
        auto room = std::make_shared<MediaRoom>(kMeetingId);
        auto old_media = std::make_shared<MediaSession>(kMeetingId, kUid, 801, old_connection.session,
            [](std::uint64_t, MediaSignalResponse) {},
            [gate](int, std::uint64_t, const std::string&) { gate->Block(); },
            [](int, std::uint64_t, const std::string&, rtc::binary) {});
        auto peer = std::make_shared<MediaPeer>(kMeetingId, kUid);
        peer->SetSession(old_media);
        room->AddPeer(peer);
        logic->m_rooms.emplace(kMeetingId, room);
        PostRequest(*logic, old_connection, 801, "offer", publisher.offer);
        gate->WaitEntered();

        // 同 Worker 的另一房间还未创建，断开登记必须覆盖已经入队但尚未执行的 Offer。
        const auto queued_room = kMeetingId + 4;
        Expect(logic->m_room_workers.WorkerIndex(queued_room) ==
               logic->m_room_workers.WorkerIndex(kMeetingId), "测试未使用同一个 Worker");
        PostRequest(*logic, old_connection, 802, "offer", publisher.offer, kUid, queued_room);
        Expect(!FindRoom(*logic, queued_room), "排队的 Offer 提前执行");
        old_connection.session->Close();
        logic->PostSignalDisconnected(old_connection.session);
        // 新旧连接重用所有业务编号，必须由连接对象身份区分。
        PostRequest(*logic, new_connection, 801, "offer", publisher.offer);
        PostLocal(*logic, old_connection.session, old_media->GetInstanceId(), 801, "answer", "stale");
        logic->m_room_workers.Post(kMeetingId,
            PublisherTrackReadyEvent{kMeetingId, kUid, old_media->GetInstanceId(), "video"});
        gate->Release();
        WaitAnswerAndCandidate(new_connection, 801);
        const auto current = Current(*logic);
        Expect(current && current != old_media && current->GetSignalSession() == new_connection.session,
               "新连接复用了旧媒体会话或旧归属");
        Expect(room->PeerCount() == 0 && !FindRoom(*logic, queued_room),
               "在途旧会话或未创建房间的排队 Offer 未被清理");

        // 绕过入队入口直接放入旧事件，验证执行时的停止检查；错误的有效连接也必须被拒绝。
        for (const auto& type : {"offer", "answer", "candidate", "leave"}) {
            MediaSignalRequest request;
            request.meeting_id = kMeetingId;
            request.uid = kUid;
            request.signal_id = 801;
            request.signal_type = type;
            request.sdp = publisher.offer;
            request.candidate = "invalid-old-candidate";
            request.mid = "video";
            logic->m_room_workers.Post(kMeetingId, MediaSignalTask{old_connection.session, request});
        }
        PostRequest(*logic, unrelated_connection, 801, "answer", "invalid-sdp");
        PostRequest(*logic, unrelated_connection, 801, "candidate");
        PostRequest(*logic, unrelated_connection, 801, "leave");
        // 即便伪造当前实例号，错误连接的本地信令也不能进入其 UDS 发送队列。
        PostLocal(*logic, unrelated_connection.session, current->GetInstanceId(), 801, "candidate", "stale");
        logic->PostSignalDisconnected(old_connection.session);
        PostLocal(*logic, new_connection.session, current->GetInstanceId(), 801, "candidate", "new-owner");
        WaitMarker(new_connection, "new-owner");
        Expect(Current(*logic) == current, "旧连接事件或重复清理影响了新会话");
        char byte;
        Expect(::recv(unrelated_connection.peer_fd, &byte, 1, MSG_DONTWAIT) == -1 &&
               (errno == EAGAIN || errno == EWOULDBLOCK), "错误归属的本地信令被发送");

        // 故意先建立新会话再投旧清理：即使清理晚到也必须按连接筛选。
        new_connection.session->Close();
        PostRequest(*logic, unrelated_connection, 801, "offer", publisher.offer);
        WaitAnswerAndCandidate(unrelated_connection, 801);
        const auto newest = Current(*logic);
        Expect(newest != current, "相同 signal_id 的新连接未替换媒体实例");
        logic->PostSignalDisconnected(new_connection.session);
        PostLocal(*logic, unrelated_connection.session, newest->GetInstanceId(), 801, "candidate", "late-cleanup");
        WaitMarker(unrelated_connection, "late-cleanup");
        Expect(Current(*logic) == newest, "晚到的旧连接清理删除了新会话");
        unrelated_connection.session->Close();
        logic->PostSignalDisconnected(unrelated_connection.session);
        WaitUntil([&]() { return !FindRoom(*logic); }, "新连接断开后房间未清理");
    }

    static void TestDisconnectSourcesAndRoomOwnership()
    {
        // 三种停止来源都经 HandleRead 退出，再走与 CServer 相同的 Close/通知顺序。
        for (int reason = 0; reason < 3; ++reason) {
            auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
            Connection disconnected;
            Connection survivor;
            std::vector<std::shared_ptr<MediaSession>> old_media;
            for (std::uint64_t offset = 0; offset < 4; ++offset) {
                const auto meeting_id = kMeetingId + offset;
                auto room = std::make_shared<MediaRoom>(meeting_id);
                {
                    std::lock_guard<std::mutex> lock(logic->m_rooms_mutex);
                    logic->m_rooms.emplace(meeting_id, room);
                }
                auto media = std::make_shared<MediaSession>(meeting_id, kUid, 901, disconnected.session,
                    [](std::uint64_t, MediaSignalResponse) {},
                    [](int, std::uint64_t, const std::string&) {},
                    [](int, std::uint64_t, const std::string&, rtc::binary) {});
                auto peer = std::make_shared<MediaPeer>(meeting_id, kUid);
                peer->SetSession(media);
                room->AddPeer(peer);
                old_media.push_back(media);
                // 有效但不匹配的 Leave 不改成员，仅通过生产入口登记该连接涉及的房间。
                PostRequest(*logic, disconnected, 999, "leave", "", kUid, meeting_id);
            }
            auto other = std::make_shared<MediaSession>(kMeetingId, kUid + 1, 902, survivor.session,
                [](std::uint64_t, MediaSignalResponse) {},
                [](int, std::uint64_t, const std::string&) {},
                [](int, std::uint64_t, const std::string&, rtc::binary) {});
            auto other_peer = std::make_shared<MediaPeer>(kMeetingId, kUid + 1);
            other_peer->SetSession(other);
            FindRoom(*logic)->AddPeer(other_peer);
            other->AddOutgoingTrack(kUid, old_media.front()->GetInstanceId(), "video", 1234);

            if (reason == 0) {
                ::shutdown(disconnected.peer_fd, SHUT_WR); // 对端结束发送，读取遇到 EOF。
            }
            else if (reason == 1) {
                ::shutdown(disconnected.peer_fd, SHUT_RD); // 对端不再接收，实际 send 失败。
                Expect(disconnected.session->Send("trigger-send-failure"), "测试发送未入队");
                WaitUntil([&]() { return disconnected.session->IsStopped(); }, "发送失败未停止连接");
            }
            else {
                Expect(!disconnected.session->Send(std::string(4 * 1024 * 1024, 'x')),
                       "发送队列超限未停止连接");
            }
            Expect(!disconnected.session->HandleRead(), "停止来源未使读取循环退出");
            // 清理尚未入队时，实例号仍匹配的旧 Track 回调也必须因连接停止而被拒绝。
            logic->m_room_workers.Post(kMeetingId,
                PublisherTrackReadyEvent{kMeetingId, kUid, old_media.front()->GetInstanceId(), "video"});
            logic->m_room_workers.Post(kMeetingId,
                PublisherTrackReadyEvent{kMeetingId, kUid, old_media.front()->GetInstanceId(), "audio"});
            MediaSignalResponse marker;
            marker.meeting_id = kMeetingId;
            marker.uid = kUid + 1;
            marker.signal_id = 902;
            marker.signal_type = "candidate";
            marker.candidate = "before-cleanup";
            logic->m_room_workers.Post(kMeetingId,
                LocalMediaSignalEvent{survivor.session, other->GetInstanceId(), marker});
            WaitMarker(survivor, "before-cleanup");
            {
                std::lock_guard<std::mutex> lock(other->m_track_mutex);
                Expect(other->m_outgoing_audio_tracks.count(kUid) == 0,
                       "停止连接的 Track 回调仍创建了消费 Track");
            }
            disconnected.session->Close();
            logic->PostSignalDisconnected(disconnected.session);
            logic->PostSignalDisconnected(disconnected.session);
            WaitUntil([&]() {
                return !Current(*logic) && !FindRoom(*logic, kMeetingId + 1) &&
                       !FindRoom(*logic, kMeetingId + 2) && !FindRoom(*logic, kMeetingId + 3);
            }, "断开清理遗漏了不同 Worker 的房间");
            // 同房间保留另一条连接的成员，并用其回调作为清理完成标记。
            marker.candidate = "survivor";
            logic->m_room_workers.Post(kMeetingId,
                LocalMediaSignalEvent{survivor.session, other->GetInstanceId(), marker});
            WaitMarker(survivor, "survivor");
            Expect(Current(*logic, kUid + 1) == other, "断开清理删除了其他连接的成员");
            {
                std::lock_guard<std::mutex> lock(other->m_track_mutex);
                Expect(other->m_outgoing_video_tracks.count(kUid) == 0, "旧连接消费 Track 未清理");
            }
            PostRequest(*logic, survivor, 902, "leave", "", kUid + 1);
            WaitUntil([&]() { return !FindRoom(*logic); }, "剩余成员无法正常离会");
        }
    }
};

int main()
{
    try {
        LocalSignalTestAccess::TestDispatchAndReplacement();
        LocalSignalTestAccess::TestCallbacksAfterDestruction();
        LocalSignalTestAccess::TestControlErrors();
        LocalSignalTestAccess::TestCleanupIdentityAndTrackFailure();
        LocalSignalTestAccess::TestDisconnectWithQueuedSignals();
        LocalSignalTestAccess::TestDisconnectSourcesAndRoomOwnership();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
