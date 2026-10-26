#include "net/CServer.h"
#include "common/global.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
void Expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Pair {
    int fd[2]{-1, -1};
    Pair() { Expect(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, fd) == 0, "socketpair 失败"); }
    ~Pair() { for (auto value : fd) if (value >= 0) ::close(value); }
    int Release() { const int value = fd[0]; fd[0] = -1; return value; }
};

std::string Ping(const std::string& payload)
{
    std::string frame(4, '\0');
    frame[0] = ID_HEARTBEAT_REQUEST >> 8U;
    frame[1] = ID_HEARTBEAT_REQUEST & 255;
    frame[2] = payload.size() >> 8U;
    frame[3] = payload.size() & 255;
    return frame + payload;
}

void Write(int fd, const std::string& data)
{
    Expect(::send(fd, data.data(), data.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(data.size()), "测试发送失败");
}
}

struct HeartbeatTestAccess
{
    static void TestProtocol()
    {
        using namespace std::chrono;
        Pair pair;
        auto session = std::make_shared<Session>(pair.fd[0]);
        const auto now = steady_clock::now();
        Expect(session->ConnectionExpired(now + seconds(31)), "未鉴权连接未按独立期限过期");
        session->SetUserId(42);
        Expect(!session->ConnectionExpired(now + seconds(31)), "未启用心跳的旧客户端被误判");
        const auto frame = Ping("{\"seq\":\"1\"}");
        Write(pair.fd[1], frame.substr(0, 6));
        Expect(session->HandleRead() && !session->m_heartbeat_enabled, "半包错误地刷新心跳");
        Write(pair.fd[1], frame.substr(6));
        Expect(session->HandleRead() && session->HandleWrite(), "序号心跳收发失败");
        char reply[128];
        const auto length = ::recv(pair.fd[1], reply, sizeof(reply), 0);
        Expect(length > 4 && std::string(reply + 4, length - 4) == "{\"seq\":\"1\"}", "PONG 未回显序号");
        session->m_last_heartbeat = steady_clock::now() - seconds(21);
        const auto previous = session->m_last_heartbeat;
        Write(pair.fd[1], frame);
        Expect(session->HandleRead() && !session->HasPendingWrite(), "重复 PING 产生额外响应");
        Expect(session->m_last_heartbeat == previous, "重放延长了存活期限");
        const auto expired = steady_clock::now();
        Expect(!session->ConnectionExpired(expired, true), "长暂停后未提供复查机会");
        Expect(session->ConnectionExpired(expired + seconds(4), true), "复查被反复延期");
        Write(pair.fd[1], Ping("{\"seq\":\"2\"}"));
        Expect(session->HandleRead() && !session->ConnectionExpired(steady_clock::now()), "新序号未恢复存活状态");
    }

    static void TestInvalidAndFlood()
    {
        for (const auto& payload : {std::string("[]"), std::string("{\"seq\":0}"),
                 std::string("{\"seq\":\"01\"}"), std::string("{\"seq\":\"18446744073709551616\"}"),
                 std::string("{} trailing"), std::string("{\"seq\":\"1\",\"seq\":\"2\"}"), std::string(97, ' ')}) {
            Pair pair;
            auto session = std::make_shared<Session>(pair.fd[0]);
            session->SetUserId(42);
            Write(pair.fd[1], Ping(payload));
            Expect(!session->HandleRead() && session->IsClosed(), "非法心跳未关闭连接");
            Expect(!session->m_heartbeat_enabled, "非法心跳启用了存活期限");
        }
        {
            Pair pair;
            auto session = std::make_shared<Session>(pair.fd[0]);
            Write(pair.fd[1], Ping("{}"));
            Expect(!session->HandleRead() && session->IsClosed(), "未鉴权 PING 被接受");
        }
        {
            Pair pair;
            auto session = std::make_shared<Session>(pair.fd[0]);
            session->SetUserId(42);
            Write(pair.fd[1], Ping("{}"));
            Expect(session->HandleRead() && session->m_heartbeat_enabled, "旧版空对象心跳不兼容");
            Write(pair.fd[1], Ping("{\"seq\":\"1\"}"));
            Expect(session->HandleRead(), "序号模式启用失败");
            Write(pair.fd[1], Ping("{}"));
            Expect(!session->HandleRead(), "序号模式被降级为空对象");
        }
        Pair pair;
        auto session = std::make_shared<Session>(pair.fd[0]);
        session->SetUserId(42);
        for (unsigned index = 1; index <= 5; ++index) {
            Write(pair.fd[1], Ping("{\"seq\":\"" + std::to_string(index) + "\"}"));
            Expect(session->HandleRead() == (index <= 4), "心跳频率上限未生效");
        }
    }

    static void TestBoundedScanAndFreshInput()
    {
        using namespace std::chrono;
        MediaSignalClient media([](auto, auto, auto) {});
        CServer server(0, media);
        server.m_epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
        Expect(server.m_epoll_fd >= 0, "epoll 创建失败");
        // Session 拥有真实 fd，但不启动端口、数据库或后台 I/O 线程。
        for (std::uint64_t token = 5; token < 135; ++token) {
            Pair pair;
            auto session = std::make_shared<Session>(pair.Release(), nullptr, token);
            session->SetUserId(42);
            session->m_heartbeat_enabled = true;
            session->m_last_heartbeat = steady_clock::now() - seconds(21);
            server.m_sessions.emplace(token, session);
            server.AddToEpoll(session->GetFd(), EPOLLIN, token);
        }
        server.m_heartbeat_scanning = true;
        server.m_heartbeat_through = 134;
        server.CheckHeartbeats();
        Expect(server.m_sessions.size() == 66, "单轮扫描未限制为 64 条");
        server.CheckHeartbeats();
        Expect(server.m_sessions.size() == 2, "扫描未从上次 token 继续");
        server.CheckHeartbeats();
        Expect(server.m_sessions.empty() && !server.m_heartbeat_scanning, "扫描没有正常结束");

        Pair pair;
        auto session = std::make_shared<Session>(pair.Release(), nullptr, 200);
        session->SetUserId(42);
        session->m_heartbeat_enabled = true;
        session->m_last_heartbeat = steady_clock::now() - seconds(21);
        server.m_sessions.emplace(200, session);
        server.AddToEpoll(session->GetFd(), EPOLLIN, 200);
        Write(pair.fd[1], Ping("{\"seq\":\"1\"}"));
        server.m_heartbeat_scanning = true;
        server.m_heartbeat_after = 0;
        server.m_heartbeat_through = 200;
        server.CheckHeartbeats();
        Expect(server.m_sessions.count(200) == 1, "扫描未先处理已到达的有效 PING");
        server.CloseClient(199); // 旧 token 不得关闭新连接。
        Expect(server.m_sessions.count(200) == 1, "旧身份误伤新连接");
        server.CloseClient(200);
        server.CloseClient(200); // 重复关闭应无副作用。
    }
};

int main()
{
    try {
        HeartbeatTestAccess::TestProtocol();
        HeartbeatTestAccess::TestInvalidAndFlood();
        HeartbeatTestAccess::TestBoundedScanAndFreshInput();
        std::cout << "心跳协议、重放、限流、期限、分批扫描与关闭复用验证通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
