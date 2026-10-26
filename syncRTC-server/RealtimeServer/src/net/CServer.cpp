#include "net/CServer.h"
#include "service/LogicSystem.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace {

constexpr int kMaxEvents = 64;
constexpr std::uint64_t kListenToken = 1;
constexpr std::uint64_t kTimerToken = 2;
constexpr std::uint64_t kWakeToken = 3;
constexpr std::uint64_t kSignalToken = 4;

std::runtime_error MakeSystemError(const std::string& action)
{
    return std::runtime_error(action + "：" + std::strerror(errno));
}

} // namespace

CServer::CServer(unsigned short port, MediaSignalClient& media_client)
    : m_port(port), m_media_client(media_client)
{
}

CServer::~CServer() { Shutdown(); }

void CServer::Start(const std::string& media_socket_path)
{
    if (m_epoll_fd != -1) {
        throw std::logic_error("CServer 已启动");
    }
    m_wakeup = std::make_shared<IoWakeup>();
    m_listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_listen_fd == -1) {
        throw MakeSystemError("创建监听 socket 失败");
    }

    const int reuse_address = 1;
    if (::setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_address,
                     sizeof(reuse_address)) == -1) {
        throw MakeSystemError("设置 SO_REUSEADDR 失败");
    }

    sockaddr_in listen_address{};
    listen_address.sin_family = AF_INET;
    listen_address.sin_addr.s_addr = htonl(INADDR_ANY);
    listen_address.sin_port = htons(m_port);

    if (::bind(m_listen_fd, reinterpret_cast<const sockaddr*>(&listen_address),
               sizeof(listen_address)) == -1) {
        throw MakeSystemError("绑定监听端口失败");
    }
    if (::listen(m_listen_fd, SOMAXCONN) == -1) {
        throw MakeSystemError("开始监听失败");
    }

    m_epoll_fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epoll_fd == -1) {
        throw MakeSystemError("创建 epoll 失败");
    }
    AddToEpoll(m_listen_fd, EPOLLIN, kListenToken);
    AddToEpoll(m_wakeup->GetFd(), EPOLLIN, kWakeToken);

    m_timer_fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (m_timer_fd == -1) {
        throw MakeSystemError("创建 timerfd 失败");
    }

    itimerspec timer_spec{};
    timer_spec.it_value.tv_sec = 1;
    timer_spec.it_interval.tv_sec = 1;
    if (::timerfd_settime(m_timer_fd, 0, &timer_spec, nullptr) == -1) {
        throw MakeSystemError("设置 timerfd 失败");
    }
    AddToEpoll(m_timer_fd, EPOLLIN, kTimerToken);
    sigset_t signals;
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGTERM);
    ::sigaddset(&signals, SIGINT);
    m_signal_fd = ::signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_signal_fd == -1) throw MakeSystemError("创建 signalfd 失败");
    AddToEpoll(m_signal_fd, EPOLLIN, kSignalToken);
    m_media_client.Start(media_socket_path, m_epoll_fd, m_wakeup);

    std::cout << "RealtimeServer 正在监听 0.0.0.0:" << m_port << std::endl;
}

void CServer::Run()
{
    try {
        while (!m_wakeup->Stopping()) {
            RunOnce();
        }
    } catch (...) {
        Shutdown();
        throw;
    }
    Shutdown();
}

void CServer::Stop()
{
    // 调用方在 Start 完成后使用；与 I/O 收尾并发时唤醒对象仍保活。
    if (m_wakeup) {
        m_wakeup->RequestStop();
    }
}

void CServer::RunOnce(int max_wait_ms)
{
    m_media_client.Tick();
    int timeout = m_media_client.NextTimeoutMs();
    if (m_heartbeat_scanning) timeout = 0;
    if (max_wait_ms >= 0 && (timeout < 0 || timeout > max_wait_ms)) {
        timeout = max_wait_ms;
    }
    std::array<epoll_event, kMaxEvents> events{};
    const int count = ::epoll_wait(m_epoll_fd, events.data(), kMaxEvents, timeout);
    if (count == -1) {
        if (errno == EINTR) {
            return;
        }
        throw MakeSystemError("等待 epoll 事件失败");
    }
    for (int index = 0; index < count && !m_wakeup->Stopping(); ++index) {
        const auto token = events[index].data.u64;
        const auto flags = events[index].events;
        if (token == kListenToken) {
            if (flags & (EPOLLERR | EPOLLHUP)) {
                throw std::runtime_error("监听 socket 发生异常");
            }
            AcceptClients();
        } else if (token == kTimerToken) {
            HandleTimer();
        } else if (token == kWakeToken) {
            HandleWakeup();
        } else if (token == kSignalToken) {
            // 不在异步信号处理器里加锁；在唯一 I/O 线程请求正常退出。
            signalfd_siginfo signal{};
            ssize_t bytes;
            do { bytes = ::read(m_signal_fd, &signal, sizeof(signal)); } while (bytes < 0 && errno == EINTR);
            if (bytes == sizeof(signal)) {
                std::cout << "[shutdown-signal] signal=" << signal.ssi_signo << std::endl;
                Stop();
            } else if (bytes < 0 && errno != EAGAIN) {
                throw MakeSystemError("读取 signalfd 失败");
            }
        } else if (token & MediaSignalClient::kSocketTokenBit) {
            m_media_client.HandleEvents(token, flags);
        } else {
            const auto it = m_sessions.find(token);
            if (it == m_sessions.end()) {
                continue;
            }
            const auto session = it->second;
            bool alive = true;
            // RDHUP/HUP 可能与大量尚未读完的数据同时出现。按预算继续读到 EOF，
            // 不因本轮读满预算就提前丢弃内核中已到达的尾部帧。
            if (flags & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) {
                alive = session->HandleRead();
            }
            if (alive && (flags & EPOLLOUT)) {
                alive = session->HandleWrite();
            }
            if (!alive || (flags & EPOLLERR)) {
                CloseClient(token);
            } else {
                UpdateClient(token, session);
            }
        }
    }
    // 同一循环推进连接超时、重连和由其他线程请求的通道失效/写监听。
    if (!m_wakeup->Stopping()) {
        CheckHeartbeats();
        m_media_client.Tick();
    }
}

void CServer::HandleWakeup()
{
    const auto pending = m_wakeup->Take();
    for (const auto token : pending) {
        if (m_wakeup->Stopping()) {
            return;
        }
        const auto it = m_sessions.find(token);
        if (it == m_sessions.end()) {
            continue; // UDS 的唤醒由本轮 Tick 处理；旧 TCP 身份直接丢弃。
        }
        const auto session = it->second;
        if (!session->HandleWrite()) {
            CloseClient(token);
        } else {
            UpdateClient(token, session);
        }
    }
}

void CServer::UpdateClient(std::uint64_t token, const std::shared_ptr<Session>& session)
{
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP | (session->HasPendingWrite() ? EPOLLOUT : 0U);
    event.data.u64 = token;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, session->GetFd(), &event) == -1) {
        CloseClient(token);
    }
}

void CServer::AcceptClients()
{
    // LT 监听按批次接纳，避免连接洪峰占住 UDS 与已有客户端。
    for (int accepted = 0; accepted < kMaxEvents; ++accepted) {
        sockaddr_in client_address{};
        socklen_t address_length = sizeof(client_address);
        const int client_fd = ::accept4(m_listen_fd,
                                        reinterpret_cast<sockaddr*>(&client_address),
                                        &address_length, SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }

            std::cerr << "接受客户端连接失败：" << std::strerror(errno) << std::endl;
            return;
        }

        try {
            const auto token = m_next_token++;
            auto session = std::make_shared<Session>(client_fd, m_wakeup, token);
            AddToEpoll(client_fd, EPOLLIN | EPOLLRDHUP, token);
            m_sessions.emplace(token, std::move(session));
        }
        catch (const std::exception& exception) {
            std::cerr << "登记客户端连接失败：" << exception.what() << std::endl;
            ::close(client_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN]{};
        const char* ip_text = ::inet_ntop(AF_INET, &client_address.sin_addr,
                                          client_ip, sizeof(client_ip));
        std::cout << "客户端接入：" << (ip_text != nullptr ? ip_text : "未知地址")
                  << ':' << ntohs(client_address.sin_port)
                  << "，fd=" << client_fd << std::endl;
    }
}

void CServer::HandleTimer()
{
    std::uint64_t expiration_count = 0;
    const ssize_t bytes_read = ::read(m_timer_fd, &expiration_count, sizeof(expiration_count));
    if (bytes_read == -1 && errno != EAGAIN) {
        std::cerr << "读取 timerfd 失败：" << std::strerror(errno) << std::endl;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!m_heartbeat_scanning && !m_sessions.empty()) {
        m_heartbeat_after = 0;
        m_heartbeat_through = m_sessions.rbegin()->first;
        m_heartbeat_scanning = true;
        m_heartbeat_recheck = now - m_last_timer > std::chrono::seconds(5);
    }
    m_last_timer = now;
    // 累积 tick 只启动一轮；成员宽限清理仍是独立的会议串行操作。
    LogicSystem::GetInstance()->PostReconnectTimeoutCheck();
}

void CServer::CheckHeartbeats()
{
    if (!m_heartbeat_scanning) return;
    const auto now = std::chrono::steady_clock::now();
    // 每轮最多 64 条，下一轮先继续处理 epoll，避免大量连接扫描独占 I/O。
    for (unsigned checked = 0; checked < 64; ++checked) {
        const auto it = m_sessions.upper_bound(m_heartbeat_after);
        if (it == m_sessions.end() || it->first > m_heartbeat_through) {
            m_heartbeat_scanning = false;
            return;
        }
        const auto token = it->first;
        const auto session = it->second;
        m_heartbeat_after = token;
        if (!session->ConnectionExpired(now)) continue;
        // 定时事件可能排在可读事件前面；最终判定前先读已到达的完整 PING。
        if (!session->HandleRead() || session->IsClosed() ||
            session->ConnectionExpired(now, m_heartbeat_recheck)) {
            CloseClient(token); // 复用 EOF/错误的关闭与唯一断线业务投递。
        } else {
            UpdateClient(token, session);
        }
    }
}

void CServer::CloseClient(std::uint64_t token)
{
    const auto it = m_sessions.find(token);
    if (it == m_sessions.end()) {
        return;
    }
    const auto session = it->second;
    const int fd = session->GetFd();
    session->Close();
    m_sessions.erase(it);
    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    // I/O 不读取由业务线程修改的 MeetingId；是否需要清理由业务入口判断。
    LogicSystem::GetInstance()->PostSessionDisconnected(session);
    std::cout << "客户端已断开，fd=" << fd << std::endl;
}

void CServer::AddToEpoll(int fd, std::uint32_t events, std::uint64_t token)
{
    epoll_event event{};
    event.events = events;
    event.data.u64 = token;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        throw MakeSystemError("注册 epoll 事件失败");
    }
}

void CServer::Shutdown()
{
    if (m_wakeup) {
        m_wakeup->RequestStop();
    }
    m_media_client.Stop();
    while (!m_sessions.empty()) {
        CloseClient(m_sessions.begin()->first);
    }
    if (m_timer_fd != -1) {
        ::close(m_timer_fd);
        m_timer_fd = -1;
    }
    if (m_signal_fd != -1) {
        ::close(m_signal_fd);
        m_signal_fd = -1;
    }
    if (m_listen_fd != -1) {
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }
    if (m_epoll_fd != -1) {
        ::close(m_epoll_fd);
        m_epoll_fd = -1;
    }
    if (m_wakeup) {
        m_wakeup->Close();
    }
}
