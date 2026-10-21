#include "net/MediaSignalClient.h"
#include "service/LogicSystem.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <poll.h>
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

void TestReconnectAndFrames()
{
    Listener listener;
    Events events;
    MediaSignalClient client([&](auto type, auto epoch, auto message) {
        events.Push(type, epoch, std::move(message));
    });
    // Media 尚未监听也能启动，随后服务上线自动连通。
    client.Start(listener.path);
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
    client.Stop();
    ::close(peer);
}

void TestStop()
{
    Listener listener;
    Events events;
    MediaSignalClient client([&](auto type, auto epoch, auto message) {
        events.Push(type, epoch, std::move(message));
    });
    client.Start(listener.path);
    const auto start = std::chrono::steady_clock::now();
    client.Stop();
    Expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "重连等待未被退出唤醒");
    listener.Open();
    client.Start(listener.path);
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
    client.Stop();
    sender.get();
    client.Stop();
    Expect(!client.Send(1, "after-stop"), "退出后仍接受发送");
    Expect(std::chrono::steady_clock::now() - stop_start < std::chrono::seconds(1), "慢对端阻塞了退出");
    ::close(peer);
}
} // namespace

struct MediaSignalTestAccess
{
    static void TestLogicEpochs()
    {
        // 暂停业务消费，精确控制旧事件延迟；I/O 使用真实连接，业务判定使用生产函数。
        auto logic = std::shared_ptr<LogicSystem>(new LogicSystem);
        {
            std::lock_guard<std::mutex> lock(logic->m_mutex);
            logic->m_stop = true;
        }
        logic->m_cond.notify_one();
        logic->work_thread.join();
        Listener listener;
        listener.Open();
        logic->StartMediaSignalClient(listener.path);
        int peer = listener.Accept();
        const auto next_event = [&]() {
            std::unique_lock<std::mutex> lock(logic->m_mutex);
            Expect(logic->m_cond.wait_for(lock, std::chrono::seconds(6), [&]() {
                return !logic->m_msg_que.empty();
            }), "业务队列没有收到 UDS 事件");
            auto event = logic->m_msg_que.front();
            logic->m_msg_que.pop();
            return event;
        };
        auto up = next_event();
        logic->AcceptMediaEvent(*up);
        auto session = std::make_shared<Session>(-1);
        session->SetMediaSignalId(10);
        logic->m_media_signal_sessions[10] = session;
        Write(peer, EncodeFrame("old-response"));
        auto old_response = next_event();
        Expect(logic->AcceptMediaEvent(*old_response), "当前代次响应被拒绝");
        ::close(peer);
        auto down = next_event();
        Expect(!logic->AcceptMediaEvent(*old_response), "断线通知处理前仍接受旧响应");
        logic->AcceptMediaEvent(*down);
        Expect(session->GetMediaSignalId() == 0 && logic->m_media_signal_sessions.empty(), "断线未清理媒体关联");
        peer = listener.Accept();
        auto new_up = next_event();
        logic->AcceptMediaEvent(*new_up);
        Expect(new_up->media_epoch > up->media_epoch, "重连未更新代次");
        session->SetMediaSignalId(20);
        logic->m_media_signal_sessions[20] = session;
        logic->AcceptMediaEvent(*down);
        logic->AcceptMediaEvent(*up);
        Expect(!logic->AcceptMediaEvent(*old_response), "重连后接受了旧响应");
        Expect(session->GetMediaSignalId() == 20 && logic->m_media_signal_sessions.count(20) == 1,
               "旧连接事件清理了新媒体关联");
        Write(peer, EncodeFrame("new-response"));
        Expect(logic->AcceptMediaEvent(*next_event()), "新连接响应未被接受");
        Expect(logic->SendMediaSignal("new-request") && ReadFrame(peer) == "new-request", "业务发送未使用新代次");
        logic->m_media_client.Stop();
        ::close(peer);
    }
};

int main()
{
    try {
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
