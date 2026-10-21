#include "net/Session.h"

#include <array>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
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

// 使用真实 UDS socketpair，不启动 RealtimeServer；小发送缓冲区便于覆盖部分写入和阻塞。
struct Connection
{
    Connection()
    {
        int sockets[2];
        Expect(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0, "创建 UDS 失败");
        session = std::make_shared<Session>(sockets[0]);
        peer_fd = sockets[1];
        int buffer_size = 4096;
        Expect(::setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF,
                           &buffer_size, sizeof(buffer_size)) == 0, "设置发送缓冲区失败");
        timeval timeout{2, 0};
        Expect(::setsockopt(peer_fd, SOL_SOCKET, SO_RCVTIMEO,
                           &timeout, sizeof(timeout)) == 0, "设置测试读取超时失败");
    }

    ~Connection()
    {
        session->Close();
        ClosePeer();
    }

    void ClosePeer()
    {
        if (peer_fd != -1) {
            ::close(peer_fd);
            peer_fd = -1;
        }
    }

    std::shared_ptr<Session> session;
    int peer_fd = -1;
};

void ReadAll(int fd, char* data, std::size_t length)
{
    std::size_t received = 0;
    while (received != length) {
        const ssize_t count = ::recv(fd, data + received, length - received, 0);
        Expect(count > 0, "读取信令帧失败或超时");
        received += static_cast<std::size_t>(count);
    }
}

std::string ReadFrame(int fd)
{
    std::array<unsigned char, 4> header{};
    ReadAll(fd, reinterpret_cast<char*>(header.data()), header.size());
    const std::uint32_t length = (static_cast<std::uint32_t>(header[0]) << 24U) |
        (static_cast<std::uint32_t>(header[1]) << 16U) |
        (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
    Expect(length <= 4 * 1024 * 1024, "帧头损坏或发生串帧");
    std::string payload(length, '\0');
    ReadAll(fd, payload.data(), payload.size());
    return payload;
}

void TestFramesAndConcurrentSend()
{
    Connection connection;
    const std::vector<std::string> frames{"", std::string("a\0b", 3), std::string(256 * 1024, 'x')};
    for (const auto& frame : frames) {
        Expect(connection.session->Send(frame), "顺序帧入队失败");
    }
    for (const auto& frame : frames) {
        Expect(ReadFrame(connection.peer_fd) == frame, "帧顺序或部分写入内容错误");
    }

    // 多个业务线程同时发送：不同生产者可以交错，同一生产者的帧必须保持顺序且不丢失。
    std::vector<std::future<bool>> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.push_back(std::async(std::launch::async, [&, producer]() {
            for (int index = 0; index < 40; ++index) {
                if (!connection.session->Send(std::to_string(producer) + ":" + std::to_string(index))) {
                    return false;
                }
            }
            return true;
        }));
    }
    for (auto& producer : producers) {
        Expect(producer.get(), "并发入队失败");
    }
    std::array<int, 4> next{};
    for (int index = 0; index < 160; ++index) {
        const auto frame = ReadFrame(connection.peer_fd);
        const int producer = frame.at(0) - '0';
        Expect(producer >= 0 && producer < 4, "生产者编号损坏");
        Expect(frame == std::to_string(producer) + ":" + std::to_string(next[producer]++),
               "并发发送串帧或同一生产者顺序错误");
    }
    for (int count : next) {
        Expect(count == 40, "并发发送丢帧");
    }
}

void TestCloseInterruptsIo()
{
    Connection connection;
    Expect(connection.session->Send(std::string(512 * 1024, 'x')), "大帧入队失败");
    char byte;
    ReadAll(connection.peer_fd, &byte, 1);
    // 对端只读一个字节就停止，剩余数据远超发送缓冲区；Send 仍应完成入队。
    Expect(connection.session->Send("queued"), "慢对端导致入队失败");
    auto reader = std::async(std::launch::async, [&]() { return connection.session->HandleRead(); });
    const bool read_waiting = reader.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout;
    const auto started = std::chrono::steady_clock::now();
    connection.session->Close();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    Expect(!reader.get() && read_waiting, "Close 未中断阻塞读取");
    Expect(elapsed < std::chrono::seconds(2), "Close 未及时中断阻塞发送");
    Expect(connection.session->GetFd() == -1 && !connection.session->Send("late"),
           "关闭后仍接受发送或未释放 fd");

    // 新连接通常复用刚关闭的 fd。旧 Session 再关闭、再发送都不能影响新连接。
    Connection replacement;
    connection.session->Close();
    Expect(!connection.session->Send("old"), "旧 Session 接受了迟到消息");
    Expect(replacement.session->Send("new"), "旧 Session 关闭影响了新连接");
    Expect(ReadFrame(replacement.peer_fd) == "new", "新连接数据被旧 Session 污染");
}

void TestQueueLimit()
{
    Connection connection;
    const std::string payload(512 * 1024, 'x');
    Expect(connection.session->Send(payload), "第一帧入队失败");
    char byte;
    ReadAll(connection.peer_fd, &byte, 1);
    bool rejected = false;
    for (int index = 0; index < 16; ++index) {
        if (!connection.session->Send(payload)) {
            rejected = true;
            break;
        }
    }
    Expect(rejected && !connection.session->Send("late"), "队列超限后未停用连接");
    Expect(!connection.session->HandleRead(), "队列超限后未唤醒接收端");
    connection.session->Close();

    // 用小帧验证帧数上限，避免只限制字节数时大量空帧积压。
    Connection small_frames;
    Expect(small_frames.session->Send(payload), "阻塞帧入队失败");
    ReadAll(small_frames.peer_fd, &byte, 1);
    int accepted = 0;
    while (accepted < 1100 && small_frames.session->Send("")) {
        ++accepted;
    }
    Expect(accepted == 1023, "帧数上限没有计入正在发送的帧");
}

void TestPeerDisconnectAndConcurrentClose()
{
    Connection connection;
    Expect(connection.session->Send(std::string(512 * 1024, 'x')), "断连测试入队失败");
    char byte;
    ReadAll(connection.peer_fd, &byte, 1);
    connection.ClosePeer();
    // 不主动读取 EOF，让发送线程自身发现 EPIPE 并停止连接；MSG_NOSIGNAL 应避免进程退出。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (connection.session->Send("late")) {
        Expect(std::chrono::steady_clock::now() < deadline, "发送失败后未停止接收新帧");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto close_one = std::async(std::launch::async, [&]() { connection.session->Close(); });
    auto close_two = std::async(std::launch::async, [&]() { connection.session->Close(); });
    close_one.get();
    close_two.get();
    Expect(connection.session->GetFd() == -1, "并发关闭后未释放 fd");
}

void TestSendDuringClose()
{
    Connection connection;
    std::promise<void> start;
    const auto ready = start.get_future().share();
    std::vector<std::future<void>> producers;
    for (int index = 0; index < 4; ++index) {
        producers.push_back(std::async(std::launch::async, [&, ready]() {
            ready.wait();
            for (int frame = 0; frame < 100; ++frame) {
                if (!connection.session->Send("concurrent-close")) {
                    break;
                }
            }
        }));
    }
    start.set_value();
    connection.session->Close();
    for (auto& producer : producers) {
        producer.get();
    }
    Expect(!connection.session->Send("after-close"), "发送与关闭竞争后仍接受新帧");
}

} // namespace

int main()
{
    try {
        TestFramesAndConcurrentSend();
        TestCloseInterruptsIo();
        TestQueueLimit();
        TestPeerDisconnectAndConcurrentClose();
        TestSendDuringClose();
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
