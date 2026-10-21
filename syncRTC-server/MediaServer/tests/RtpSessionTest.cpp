#include "media/MediaPeer.h"
#include "media/MediaRoom.h"
#include "media/MediaSession.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

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

rtc::binary Packet(bool video, std::uint16_t sequence)
{
    rtc::binary packet(13);
    packet[0] = std::byte{0x80};
    auto* header = reinterpret_cast<rtc::RtpHeader*>(packet.data());
    header->setPayloadType(video ? 96 : 111);
    header->setSeqNumber(sequence);
    header->setSsrc(video ? 1234 : 1235);
    packet[12] = std::byte{0x65};
    return packet;
}

// 在真实、已 Open 的消费 Track 中观察 send 的输入，随后截住包，避免用网络等待判断“未发送”。
// 只使用 libdatachannel 的测试侧 MediaHandler，不为生产转发路径添加拦截器或虚函数。
class Recorder : public rtc::MediaHandler
{
public:
    void outgoing(rtc::message_vector& messages, const rtc::message_callback&) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& message : messages) {
            const auto* header = reinterpret_cast<const rtc::RtpHeader*>(message->data());
            m_packets.push_back({header->seqNumber(), header->ssrc()});
        }
        messages.clear();
    }

    std::size_t Count()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_packets.size();
    }

    bool Has(std::uint16_t sequence, std::uint32_t ssrc)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& packet : m_packets) {
            if (packet.first == sequence && packet.second == ssrc) {
                return true;
            }
        }
        return false;
    }

private:
    std::mutex m_mutex;
    std::vector<std::pair<std::uint16_t, std::uint32_t>> m_packets;
};

std::shared_ptr<rtc::PeerConnection> NewPeerConnection()
{
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    return std::make_shared<rtc::PeerConnection>(config);
}

void WaitGathering(const std::shared_ptr<rtc::PeerConnection>& pc)
{
    WaitUntil([&]() { return pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete; },
              "本地 ICE 候选收集未完成");
}

void WaitConnected(const std::shared_ptr<rtc::PeerConnection>& first,
                   const std::shared_ptr<rtc::PeerConnection>& second)
{
    WaitUntil([&]() {
        return first->state() == rtc::PeerConnection::State::Connected &&
               second->state() == rtc::PeerConnection::State::Connected;
    }, "本地 WebRTC 测试连接未建立");
}

} // namespace

struct RtpSessionTestAccess
{
    static constexpr int kPublisher = 20001;
    static constexpr int kSubscriber = 20002;

    static std::shared_ptr<MediaSession> AddPeer(const std::shared_ptr<MediaRoom>& room, int uid,
                                                MediaSession::MediaPacketCallback callback)
    {
        auto session = std::make_shared<MediaSession>(room->GetMeetingId(), uid, 1, std::weak_ptr<Session>{},
            [](std::uint64_t, MediaSignalResponse) {},
            [](int, std::uint64_t, const std::string&) {}, std::move(callback));
        auto peer = std::make_shared<MediaPeer>(room->GetMeetingId(), uid);
        peer->SetSession(session);
        room->AddPeer(peer);
        return session;
    }

    static void Register(const std::shared_ptr<MediaRoom>& room, int uid)
    {
        room->RegisterPublisherTrack(uid, "video");
        room->RegisterPublisherTrack(uid, "audio");
    }

    static MediaSession::OutgoingTrack Outgoing(const std::shared_ptr<MediaSession>& session,
                                               int publisher, bool video)
    {
        std::lock_guard<std::mutex> lock(session->m_track_mutex);
        return (video ? session->m_outgoing_video_tracks : session->m_outgoing_audio_tracks).at(publisher);
    }

    static void NegotiateSubscriber(const std::shared_ptr<MediaSession>& session,
                                    const std::shared_ptr<rtc::PeerConnection>& subscriber)
    {
        const auto pc = session->m_peer_connection;
        pc->setLocalDescription();
        WaitGathering(pc);
        subscriber->setRemoteDescription(*pc->localDescription());
        subscriber->setLocalDescription();
        WaitGathering(subscriber);
        pc->setRemoteDescription(*subscriber->localDescription());
        WaitConnected(pc, subscriber);
        WaitUntil([&]() {
            return Outgoing(session, kPublisher, true).track->isOpen() &&
                   Outgoing(session, kPublisher, false).track->isOpen();
        }, "消费 Track 未 Open");
    }

    static void Run()
    {
        auto room = std::make_shared<MediaRoom>(1001);
        auto received = std::make_shared<std::atomic<int>>(0);
        auto source = AddPeer(room, kPublisher,
            [weak_room = std::weak_ptr<MediaRoom>(room), received]
            (int uid, std::uint64_t session_id, const std::string& media, rtc::binary packet) {
                if (const auto current_room = weak_room.lock()) {
                    current_room->ForwardRtp(uid, session_id, media, std::move(packet));
                }
                ++*received;
            });
        const auto ignore = [](int, std::uint64_t, const std::string&, rtc::binary) {};
        auto target = AddPeer(room, kSubscriber, ignore);

        // 上行使用真实本地 ICE/DTLS/SRTP，使包进入生产代码安装的 Track::onMessage 回调。
        auto publisher = NewPeerConnection();
        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        video.addSSRC(1234, "video", "test", "video");
        auto video_track = publisher->addTrack(video);
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(111);
        audio.addSSRC(1235, "audio", "test", "audio");
        auto audio_track = publisher->addTrack(audio);
        publisher->setLocalDescription();
        WaitGathering(publisher);
        source->SetRemoteOffer(std::string(*publisher->localDescription()));
        WaitGathering(source->m_peer_connection);
        publisher->setRemoteDescription(*source->m_peer_connection->localDescription());
        WaitConnected(publisher, source->m_peer_connection);
        WaitUntil([&]() { return video_track->isOpen() && audio_track->isOpen(); }, "发布 Track 未 Open");

        Register(room, kPublisher);
        auto subscriber = NewPeerConnection();
        const auto incoming = std::make_shared<std::vector<std::shared_ptr<rtc::Track>>>();
        subscriber->onTrack([incoming](std::shared_ptr<rtc::Track> track) { incoming->push_back(track); });
        NegotiateSubscriber(target, subscriber);
        const auto old_video = Outgoing(target, kPublisher, true);
        const auto old_audio = Outgoing(target, kPublisher, false);
        Expect(old_video.publisher_session_id == source->GetInstanceId() &&
               old_audio.publisher_session_id == source->GetInstanceId(), "消费 Track 未绑定发布者会话");
        auto old_packets = std::make_shared<Recorder>();
        old_video.track->setMediaHandler(old_packets);
        old_audio.track->setMediaHandler(old_packets);
        video_track->send(Packet(true, 1));
        audio_track->send(Packet(false, 2));
        WaitUntil([&]() { return received->load() == 2; }, "真实 RTP 回调未触发");
        Expect(old_packets->Count() == 2 && old_packets->Has(1, old_video.ssrc) &&
               old_packets->Has(2, old_audio.ssrc), "当前会话的 RTP 未正确转发或 SSRC 错误");

        // 同时登记接收者为发布者，覆盖“新成员订阅已有发布者”的反向 Track 绑定。
        Register(room, kSubscriber);
        Expect(Outgoing(source, kSubscriber, true).publisher_session_id == target->GetInstanceId() &&
               Outgoing(source, kSubscriber, false).publisher_session_id == target->GetInstanceId(),
               "已有发布者的订阅 Track 身份绑定错误");

        room->RemovePeer(kPublisher);
        // 精确模拟成员已从房间删除、接收者旧 Track 尚未清理的窗口。
        // 临时保留旧绑定，确保本次拒绝来自房间入口校验，而不只是“没有消费 Track”。
        {
            std::lock_guard<std::mutex> lock(target->m_track_mutex);
            target->m_outgoing_video_tracks.emplace(kPublisher, old_video);
            target->m_outgoing_audio_tracks.emplace(kPublisher, old_audio);
        }
        video_track->send(Packet(true, 3));
        WaitUntil([&]() { return received->load() == 3; }, "离会后旧 RTP 回调未触发");
        Expect(old_packets->Count() == 2, "离会后的旧包仍被转发");
        target->RemoveOutgoingTracks(kPublisher);

        // 旧对象和旧上行连接仍活着，模拟同 UID 重入。新会话拥有新的消费 Track 和 SSRC。
        auto replacement = AddPeer(room, kPublisher, ignore);
        Register(room, kPublisher);
        NegotiateSubscriber(target, subscriber);
        const auto new_video = Outgoing(target, kPublisher, true);
        const auto new_audio = Outgoing(target, kPublisher, false);
        Expect(new_video.ssrc != old_video.ssrc &&
               new_video.publisher_session_id == replacement->GetInstanceId(), "重入后未更新消费 Track 身份");
        auto new_packets = std::make_shared<Recorder>();
        new_video.track->setMediaHandler(new_packets);
        new_audio.track->setMediaHandler(new_packets);

        audio_track->send(Packet(false, 4));
        WaitUntil([&]() { return received->load() == 4; }, "重入后旧 RTP 回调未触发");
        Expect(new_packets->Count() == 0, "旧来源回调按同 UID 的新会话转发");

        // 模拟旧包已通过房间校验，但在取得消费 Track 前发生离会重入。
        // 直接继续其第二阶段，必须由 Track 的来源实例校验拦住，不能仅依赖房间入口。
        target->ForwardRtp(kPublisher, source->GetInstanceId(), "video", Packet(true, 5));
        target->ForwardRtp(kPublisher, source->GetInstanceId(), "audio", Packet(false, 6));
        Expect(new_packets->Count() == 0, "房间校验后的在途旧包借用了新消费 Track");

        room->ForwardRtp(kPublisher, replacement->GetInstanceId(), "video", Packet(true, 7));
        room->ForwardRtp(kPublisher, replacement->GetInstanceId(), "audio", Packet(false, 8));
        Expect(new_packets->Count() == 2 && new_packets->Has(7, new_video.ssrc) &&
               new_packets->Has(8, new_audio.ssrc), "新会话被误拦截或使用了旧 SSRC");

        // 相同会议号、UID 的新房间也不能接受旧实例包，即使旧会话对象没有析构。
        auto rebuilt_room = std::make_shared<MediaRoom>(1001);
        rebuilt_room->AddPeer(room->GetPeer(kPublisher));
        rebuilt_room->AddPeer(room->GetPeer(kSubscriber));
        rebuilt_room->ForwardRtp(kPublisher, source->GetInstanceId(), "video", Packet(true, 9));
        Expect(new_packets->Count() == 2, "重建房间接受了旧实例 RTP");
        publisher->close();
        subscriber->close();
    }
};

int main()
{
    try {
        RtpSessionTestAccess::Run();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
