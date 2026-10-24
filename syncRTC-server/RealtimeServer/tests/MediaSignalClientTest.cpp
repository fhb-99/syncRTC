#include "net/MediaSignalClient.h"
#include "net/CServer.h"
#include "service/LogicSystem.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <json/json.h>
#include <stdexcept>
#include <vector>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
void Expect(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Listener
{
    Listener() : path("/tmp/syncrtc-uds-test-" + std::to_string(::getpid()) + ".sock") {}
    ~Listener() { Close(); }
    void Open()
    {
        fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        Expect(fd != -1, "创建监听 socket 失败");
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        Expect(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "绑定测试路径失败");
        Expect(::listen(fd, 4) == 0, "监听失败");
    }
    void Close()
    {
        if (fd != -1) {
            ::close(fd);
            fd = -1;
            ::unlink(path.c_str());
        }
    }
    int Accept()
    {
        pollfd descriptor{fd, POLLIN, 0};
        Expect(::poll(&descriptor, 1, 6000) > 0, "等待重连超时");
        const int client = ::accept4(fd, nullptr, nullptr, SOCK_CLOEXEC);
        Expect(client != -1, "接受连接失败");
        timeval timeout{3, 0};
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        return client;
    }
    int fd = -1;
    std::string path;
};

std::string EncodeFrame(const std::string& body)
{
    const auto size = static_cast<std::uint32_t>(body.size());
    std::string frame(4, '\0');
    frame[0] = static_cast<char>(size >> 24U);
    frame[1] = static_cast<char>(size >> 16U);
    frame[2] = static_cast<char>(size >> 8U);
    frame[3] = static_cast<char>(size);
    return frame + body;
}

void Write(int fd, const std::string& data)
{
    std::size_t offset = 0;
    while (offset != data.size()) {
        const auto count = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        Expect(count > 0, "测试对端发送失败");
        offset += static_cast<std::size_t>(count);
    }
}

void Read(int fd, char* data, std::size_t size)
{
    while (size) {
        const auto count = ::recv(fd, data, size, 0);
        Expect(count > 0, "读取帧超时或连接关闭");
        data += count;
        size -= static_cast<std::size_t>(count);
    }
}

std::string ReadFrame(int fd)
{
    std::array<unsigned char, 4> header{};
    Read(fd, reinterpret_cast<char*>(header.data()), header.size());
    const std::uint32_t size = (static_cast<std::uint32_t>(header[0]) << 24U) |
        (static_cast<std::uint32_t>(header[1]) << 16U) |
        (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
    Expect(size < 4 * 1024 * 1024, "新连接读到了旧帧尾部或损坏帧头");
    std::string body(size, '\0');
    Read(fd, body.data(), body.size());
    return body;
}

struct Events
{
    void Push(MediaSignalClient::Event type, std::uint64_t epoch, std::string message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back({type, epoch, std::move(message)});
        condition.notify_all();
    }
    void Wait(MediaSignalClient::Event type, std::uint64_t epoch, const std::string& message = {})
    {
        std::unique_lock<std::mutex> lock(mutex);
        Expect(condition.wait_for(lock, std::chrono::seconds(6), [&]() {
            for (const auto& event : events) {
                if (event.type == type && event.epoch == epoch && event.message == message) {
                    return true;
                }
            }
            return false;
        }), "等待 UDS 事件超时");
    }
    struct Entry { MediaSignalClient::Event type; std::uint64_t epoch; std::string message; };
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<Entry> events;
};

// 测试沿用真实 CServer 循环驱动 UDS，生产 MediaSignalClient 自身没有线程。
struct ServerRunner
{
    explicit ServerRunner(MediaSignalClient& client) : server(0, client) {}
    ~ServerRunner()
    {
        server.Stop();
        if (running.valid()) {
            running.wait();
        }
    }
    void Start(const std::string& path)
    {
        server.Start(path);
        running = std::async(std::launch::async, [&] { server.Run(); });
    }
    void Stop()
    {
        server.Stop();
        if (running.valid()) {
            running.get();
        }
    }
    CServer server;
    std::future<void> running;
};

void TestReconnectAndFrames()
{
    Listener listener;
    Events events;
    MediaSignalClient client([&](auto type, auto epoch, auto message) {
        events.Push(type, epoch, std::move(message));
    });
    ServerRunner loop(client);
    // Media 尚未监听也能启动，随后服务上线自动连通。
    loop.Start(listener.path);
    Expect(!client.Send(1, "offline"), "离线时接受了发送");
    listener.Open();
    int peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 1);
    Expect(client.Send(1, std::string(512 * 1024, 'a')), "大帧入队失败");
    Expect(client.Send(1, "second"), "第二帧入队失败");
    Expect(ReadFrame(peer) == std::string(512 * 1024, 'a') && ReadFrame(peer) == "second",
           "部分写入或发送顺序错误");
    const auto response = EncodeFrame("answer");
    Write(peer, response.substr(0, 2));
    Write(peer, response.substr(2) + EncodeFrame("candidate"));
    events.Wait(MediaSignalClient::Event::Message, 1, "answer");
    events.Wait(MediaSignalClient::Event::Message, 1, "candidate");

    // 不完整接收帧，以及慢对端造成的在途发送/排队帧，都不能带到下一条连接。
    Write(peer, EncodeFrame("old-response").substr(0, 6));
    Expect(client.Send(1, std::string(2 * 1024 * 1024, 'x')), "旧大帧入队失败");
    Expect(client.Send(1, "old-tail"), "旧尾帧入队失败");
    std::array<char, 4> partial_header{};
    Read(peer, partial_header.data(), partial_header.size());
    listener.Close();
    ::close(peer);
    events.Wait(MediaSignalClient::Event::Disconnected, 1);
    Expect(!client.Send(1, "late"), "断开后仍接受旧代次发送");
    listener.Open();
    peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 2);
    Expect(!client.Send(1, "old-epoch") && client.Send(2, "fresh"), "发送代次检查错误");
    Expect(ReadFrame(peer) == "fresh", "旧队列重放到了新连接");
    Write(peer, EncodeFrame("new-answer"));
    events.Wait(MediaSignalClient::Event::Message, 2, "new-answer");

    // 对端停止接收但未关闭发送方向，触发真实 send 失败而非仅靠读 EOF。
    ::shutdown(peer, SHUT_RD);
    Expect(client.Send(2, "write-error"), "发送失败测试未入队");
    events.Wait(MediaSignalClient::Event::Disconnected, 2);
    ::close(peer);
    peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 3);
    Expect(!client.Send(3, std::string(4 * 1024 * 1024, 'q')), "队列超限未失效连接");
    events.Wait(MediaSignalClient::Event::Disconnected, 3);
    ::close(peer);
    peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 4);
    // 帧长无效也应关闭并重连，不能分配任意大的接收缓冲。
    Write(peer, std::string(4, static_cast<char>(0xff)));
    events.Wait(MediaSignalClient::Event::Disconnected, 4);
    ::close(peer);
    peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 5);
    Expect(client.Send(5, "after-errors") && ReadFrame(peer) == "after-errors", "失败后恢复收发错误");
    loop.Stop();
    ::close(peer);
}

void TestStop()
{
    Listener listener;
    Events events;
    MediaSignalClient client([&](auto type, auto epoch, auto message) {
        events.Push(type, epoch, std::move(message));
    });
    ServerRunner loop(client);
    loop.Start(listener.path);
    const auto start = std::chrono::steady_clock::now();
    loop.Stop();
    Expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "重连等待未被退出唤醒");
    listener.Open();
    loop.Start(listener.path);
    const int peer = listener.Accept();
    events.Wait(MediaSignalClient::Event::Connected, 1);
    Expect(client.Send(1, std::string(2 * 1024 * 1024, 'x')), "退出测试的大帧未入队");
    std::array<char, 4> partial_header{};
    Read(peer, partial_header.data(), partial_header.size());
    // 慢对端不读数据，同时业务线程持续 Send；Stop 必须快速退出且拒绝之后的发送。
    auto sender = std::async(std::launch::async, [&]() {
        while (client.Send(1, std::string(64 * 1024, 'x'))) {}
    });
    const auto stop_start = std::chrono::steady_clock::now();
    loop.Stop();
    sender.get();
    loop.Stop();
    Expect(!client.Send(1, "after-stop"), "退出后仍接受发送");
    Expect(std::chrono::steady_clock::now() - stop_start < std::chrono::seconds(1), "慢对端阻塞了退出");
    ::close(peer);
}
} // namespace

struct MediaSignalTestAccess
{
    static void TestLogicEpochs()
    {
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        Listener listener;
        listener.Open();
        ServerRunner loop(logic->m_media_client);
        loop.Start(listener.path);
        int peer = listener.Accept();
        const auto wait_for = [](const auto& predicate) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (!predicate()) {
                Expect(std::chrono::steady_clock::now() < until, "等待媒体代次变化超时");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        wait_for([&] { return logic->m_media_epoch == 1; });
        int pair[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "信令回包 socketpair 创建失败");
        auto session = std::make_shared<Session>(pair[0]);
        session->SetMeetingId(7);
        session->SetMediaSignalId(10);
        const auto old_epoch = logic->RegisterSignal(10, session, 7);
        LogicNode old_response{{}, 0xFFFC, "old-response", 1};
        Expect(logic->AcceptMediaEvent(old_response), "当前代次响应被拒绝");

        // 保持控制队列 active_op，验证断线通知尚未应用时 IsCurrent 已拒绝旧结果。
        std::promise<std::function<void()>> paused;
        logic->m_control->Submit([&](SerialCommandQueue::Operation& op) {
            op.Wait([&](std::function<void()> resume) { paused.set_value(resume); });
        });
        auto resume = paused.get_future().get();
        ::close(peer);
        wait_for([&] { return !logic->m_media_client.IsCurrent(1); });
        Expect(!logic->AcceptMediaEvent(old_response), "断线通知处理前仍接受旧响应");
        resume();
        // IsCurrent 先失效，I/O 随后才投递断线事件；队列瞬时空闲不代表清理已完成。
        wait_for([&] { return session->GetMediaSignalId() == 0 && logic->FindSignal(10).epoch == 0; });
        logic->m_scheduler.WaitIdle();
        Expect(session->GetMediaSignalId() == 0 && logic->FindSignal(10).epoch == 0,
               "断线未清理媒体关联");

        peer = listener.Accept();
        wait_for([&] { return logic->m_media_epoch == 2; });
        session->SetMediaSignalId(20);
        logic->RegisterSignal(20, session, 7);
        LogicNode old_up{{}, 0xFFFB, "", 1};
        LogicNode old_down{{}, 0xFFFA, "", 1};
        logic->AcceptMediaEvent(old_down);
        logic->AcceptMediaEvent(old_up);
        Expect(!logic->AcceptMediaEvent(old_response), "重连后接受了旧响应");
        Expect(session->GetMediaSignalId() == 20 && logic->FindSignal(20).epoch == 2,
               "旧连接事件清理了新媒体关联");
        LogicNode fresh{{}, 0xFFFC, "new-response", 2};
        Expect(logic->AcceptMediaEvent(fresh), "新连接响应未被接受");
        Expect(!logic->SendMediaSignal("stale-request", old_epoch), "重连后业务发送错误使用了新的通道代次");
        Expect(logic->SendMediaSignal("new-request", 2) && ReadFrame(peer) == "new-request",
               "业务发送未使用新代次");
        Write(peer, EncodeFrame(R"({"signal_id":20,"signal_type":"answer","sdp":"fresh-sdp"})"));
        wait_for([&] { return session->HasPendingWrite(); });
        Expect(session->HandleWrite(), "会议队列中的媒体响应未送回 TCP");
        std::array<unsigned char, 4> header{};
        Read(pair[1], reinterpret_cast<char*>(header.data()), header.size());
        std::string answer((header[2] << 8U) | header[3], '\0');
        Read(pair[1], answer.data(), answer.size());
        Json::Reader reader;
        Json::Value json;
        Expect(((header[0] << 8U) | header[1]) == ID_MEDIA_ANSWER_RESPONSE &&
            reader.parse(answer, json) && json["sdp"].asString() == "fresh-sdp", "媒体响应路由或内容错误");

        // 旧结果已通过控制层校验，但仍在会议 commands 中等待；重连后必须再次校验。
        std::promise<std::function<void()>> room_paused;
        Expect(logic->SubmitRoom(7, [&](auto& op, auto&) {
            op.Wait([&](auto done) { room_paused.set_value(done); });
        }), "无法暂停目标会议");
        auto resume_room = room_paused.get_future().get();
        const auto flush_control = [&] {
            std::promise<void> barrier;
            Expect(logic->m_control->Submit([&](auto&) { barrier.set_value(); }), "控制屏障提交失败");
            barrier.get_future().wait();
        };
        logic->PostMediaEvent(MediaSignalClient::Event::Message, 2,
            R"({"signal_id":20,"signal_type":"answer","sdp":"queued-old-sdp"})");
        flush_control();
        ::close(peer);
        wait_for([&] { return !logic->m_media_client.IsCurrent(2); });
        peer = listener.Accept();
        wait_for([&] { return logic->m_media_epoch == 3; });
        session->SetMediaSignalId(30);
        logic->RegisterSignal(30, session, 7);
        logic->PostMediaEvent(MediaSignalClient::Event::Message, 3,
            R"({"signal_id":30,"signal_type":"answer","sdp":"current-sdp"})");
        flush_control();
        resume_room();
        logic->m_scheduler.WaitIdle();
        Expect(session->HandleWrite(), "新代次会议回包失败");
        Read(pair[1], reinterpret_cast<char*>(header.data()), header.size());
        answer.assign((header[2] << 8U) | header[3], '\0');
        Read(pair[1], answer.data(), answer.size());
        Expect(reader.parse(answer, json) && json["sdp"].asString() == "current-sdp",
               "旧媒体结果在会议排队期间跨越重连后仍被应用");
        loop.Stop();
        logic->m_scheduler.WaitIdle();
        session->Close();
        ::close(pair[0]);
        ::close(pair[1]);
        ::close(peer);
    }

};


struct SingleIoTestAccess
{
    struct Socket
    {
        int fd = -1;
        ~Socket() { if (fd != -1) ::close(fd); }
    };

    static std::string TcpFrame(std::uint16_t type, const std::string& body)
    {
        std::string frame(4, '\0');
        frame[0] = static_cast<char>(type >> 8U);
        frame[1] = static_cast<char>(type);
        frame[2] = static_cast<char>(body.size() >> 8U);
        frame[3] = static_cast<char>(body.size());
        return frame + body;
    }

    static bool Readable(int fd)
    {
        pollfd descriptor{fd, POLLIN, 0};
        return ::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & POLLIN);
    }

    static void TestWakeupBatches()
    {
        IoWakeup wakeup;
        for (std::uint64_t token = 1; token <= 130; ++token) {
            Expect(wakeup.Notify(token) && wakeup.Notify(token), "唤醒提交失败");
        }
        std::uint64_t next = 1;
        while (next <= 130) {
            Expect(Readable(wakeup.GetFd()), "批次之间丢失了唤醒");
            const auto batch = wakeup.Take();
            Expect(!batch.empty() && batch.size() <= 64, "唤醒批次未受限");
            for (const auto token : batch) {
                Expect(token == next++, "唤醒未合并或违反 FIFO");
            }
        }
        Expect(!Readable(wakeup.GetFd()), "空队列仍产生忙循环");
        wakeup.RequestStop();
        Expect(Readable(wakeup.GetFd()) && !wakeup.Notify(1), "停止未唤醒或仍接纳新提交");
    }

    static void TestUnifiedLoop()
    {
        Listener listener;
        listener.Open();
        std::uint64_t connected_epoch = 0;
        std::uint64_t disconnected_epoch = 0;
        std::vector<std::thread::id> callback_threads;
        MediaSignalClient client([&](auto event, auto epoch, auto) {
            callback_threads.push_back(std::this_thread::get_id());
            if (event == MediaSignalClient::Event::Connected) connected_epoch = epoch;
            if (event == MediaSignalClient::Event::Disconnected) disconnected_epoch = epoch;
        });
        CServer server(0, client);
        server.Start(listener.path);
        // 关闭周期定时器，验证响应依靠真实 eventfd，而不是定时扫描偶然发出。
        const itimerspec disabled{};
        Expect(::timerfd_settime(server.m_timer_fd, 0, &disabled, nullptr) == 0, "停用测试定时器失败");
        const auto pump_until = [&](const auto& predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!predicate()) {
                Expect(std::chrono::steady_clock::now() < deadline, "单 I/O 循环未能及时推进");
                server.RunOnce(20);
            }
        };
        pump_until([&] { return connected_epoch == 1; });
        Socket media_peer{listener.Accept()};
        sockaddr_in address{};
        socklen_t length = sizeof(address);
        Expect(::getsockname(server.m_listen_fd, reinterpret_cast<sockaddr*>(&address), &length) == 0,
               "读取临时 TCP 端口失败");
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const auto connect_tcp = [&](Socket& peer) {
            peer.fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            Expect(peer.fd != -1 && ::connect(peer.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
                   "连接测试 TCP 失败");
            timeval timeout{3, 0};
            ::setsockopt(peer.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            pump_until([&] { return !server.m_sessions.empty(); });
        };
        Socket tcp_peer;
        connect_tcp(tcp_peer);
        const auto old_token = server.m_sessions.begin()->first;
        const auto session = server.m_sessions.begin()->second;
        // 没有新的 TCP 可读事件，异步业务响应仍必须唤醒唯一 I/O 线程。
        Expect(std::async(std::launch::async, [&] { return session->Send(2001, "wake"); }).get(),
               "异步 TCP 响应未被接纳");
        pump_until([&] { return Readable(tcp_peer.fd); });
        std::string actual(8, '\0');
        Read(tcp_peer.fd, actual.data(), actual.size());
        Expect(actual == TcpFrame(2001, "wake"), "TCP 唤醒或组帧错误");

        // UDS 对端停止读，大帧产生背压；TCP 的解析、业务回投与发送仍须推进。
        Expect(client.Send(1, std::string(2 * 1024 * 1024, 'x')), "UDS 大帧未入队");
        for (int i = 0; i < 32; ++i) server.RunOnce(0);
        std::array<char, 4> partial_header{};
        Read(media_peer.fd, partial_header.data(), partial_header.size());
        Write(tcp_peer.fd, TcpFrame(ID_AI_ASK_REQUEST, "{}"));
        pump_until([&] { return Readable(tcp_peer.fd); });
        std::array<unsigned char, 4> header{};
        Read(tcp_peer.fd, reinterpret_cast<char*>(header.data()), header.size());
        const auto type = static_cast<std::uint16_t>((header[0] << 8U) | header[1]);
        std::string reply((header[2] << 8U) | header[3], '\0');
        Read(tcp_peer.fd, reply.data(), reply.size());
        Json::Value json;
        Json::Reader reader;
        Expect(type == ID_AI_ASK_RESPONSE && reader.parse(reply, json) &&
               json["error"].asInt() == ErrorCodes::ERROR_TOKEN, "UDS 背压阻塞了 TCP 业务响应");

        // 缩小发送缓冲，真实触发 TCP 部分写，并核对所有帧的完整字节顺序。
        const int send_buffer = 4096;
        ::setsockopt(session->GetFd(), SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer));
        std::string expected;
        for (int i = 0; i < 40; ++i) {
            const std::string body(20000, static_cast<char>('a' + i % 26));
            Expect(session->Send(2002, body), "TCP 分片测试入队失败");
            expected += TcpFrame(2002, body);
        }
        auto received = std::async(std::launch::async, [&] {
            std::string data(expected.size(), '\0');
            Read(tcp_peer.fd, data.data(), data.size());
            return data;
        });
        pump_until([&] { return received.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready; });
        Expect(received.get() == expected, "TCP 部分写造成帧交错或数据丢失");

        // 不运行循环时精确填满提交队列，检查帧数上限与关闭反馈。
        for (int i = 0; i < 1024; ++i) Expect(session->Send(2003, ""), "TCP 队列提前达到上限");
        Expect(!session->Send(2003, "overflow"), "TCP 发送队列没有上限");
        pump_until([&] { return server.m_sessions.empty(); });
        Expect(!session->Send(2004, "late"), "旧 Session 关闭后仍接受响应");
        ::close(tcp_peer.fd);
        tcp_peer.fd = -1;
        connect_tcp(tcp_peer);
        const auto new_session = server.m_sessions.begin()->second;
        server.m_wakeup->Notify(old_token); // 模拟已排队的旧连接唤醒。
        Expect(new_session->Send(2004, "new"), "新连接响应未入队");
        pump_until([&] { return Readable(tcp_peer.fd); });
        actual.assign(7, '\0');
        Read(tcp_peer.fd, actual.data(), actual.size());
        Expect(actual == TcpFrame(2004, "new"), "旧通知影响了新连接");

        // socketpair 确保数据和 FIN 在一次轮询前全部就绪，稳定覆盖超过读取预算的尾包。
        int pair[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) == 0,
               "创建 FIN 测试连接失败");
        Socket fin_peer{pair[1]};
        const auto fin_token = server.m_next_token++;
        auto fin_session = std::make_shared<Session>(pair[0], server.m_wakeup, fin_token);
        server.AddToEpoll(pair[0], EPOLLIN | EPOLLRDHUP, fin_token);
        server.m_sessions.emplace(fin_token, fin_session);
        const auto tail = TcpFrame(2006, std::string(60000, 'a')) + TcpFrame(2006, std::string(60000, 'b'));
        Write(fin_peer.fd, tail);
        Expect(::shutdown(fin_peer.fd, SHUT_WR) == 0, "发送 FIN 失败");
        server.RunOnce(0);
        Expect(server.m_sessions.count(fin_token) == 1, "读取预算耗尽后因 FIN 提前关闭，丢失尾部数据");
        pump_until([&] { return server.m_sessions.count(fin_token) == 0; });

        // 配额必须包含已取走但尚未写完的 UDS 帧，而非仅计算队列长度。
        Expect(!client.Send(1, std::string(2 * 1024 * 1024, 'y')), "在途 UDS 帧未计入字节上限");
        Expect(!client.IsCurrent(1), "UDS 超额没有立即失效代次");
        pump_until([&] { return disconnected_epoch == 1; });
        ::close(media_peer.fd);
        media_peer.fd = -1;
        pump_until([&] { return connected_epoch == 2; });
        media_peer.fd = listener.Accept();
        Expect(client.Send(2, "fresh"), "重连后无法发送");
        pump_until([&] { return Readable(media_peer.fd); });
        Expect(ReadFrame(media_peer.fd) == "fresh", "旧 UDS 帧被带到新连接");
        for (int i = 0; i < 1024; ++i) Expect(client.Send(2, ""), "UDS 帧数配额提前耗尽");
        Expect(!client.Send(2, "overflow"), "UDS 1024 帧上限失效");
        pump_until([&] { return disconnected_epoch == 2; });
        Expect(!callback_threads.empty(), "没有 UDS 回调");
        for (const auto id : callback_threads) {
            Expect(id == std::this_thread::get_id(), "UDS 事件未由 TCP 所属循环执行");
        }
        server.Shutdown();
        Expect(!new_session->Send(2005, "after-stop") && !client.Send(2, "after-stop"),
               "退出后仍接受发送");
    }
};

int main()
{
    try {
        SingleIoTestAccess::TestWakeupBatches();
        SingleIoTestAccess::TestUnifiedLoop();
        TestReconnectAndFrames();
        TestStop();
        MediaSignalTestAccess::TestLogicEpochs();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
