#include "net/CServer.h"
#include "service/LogicSystem.h"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace {

constexpr int kMaxEvents = 16;

std::runtime_error MakeSystemError(const std::string& action)
{
    return std::runtime_error(action + "：" + std::strerror(errno));
}

} // namespace

CServer::CServer(unsigned short port)
    : m_port(port),
      m_listen_fd(-1),
      m_epoll_fd(-1),
      m_timer_fd(-1)
{
}

CServer::~CServer()
{
    for (const auto& client : m_client_fds) {
        ::close(client.first);
    }

    if (m_timer_fd != -1) {
        ::close(m_timer_fd);
    }
    if (m_listen_fd != -1) {
        ::close(m_listen_fd);
    }
    if (m_epoll_fd != -1) {
        ::close(m_epoll_fd);
    }
}

void CServer::Start()
{
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
    AddToEpoll(m_listen_fd, EPOLLIN);

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
    AddToEpoll(m_timer_fd, EPOLLIN);

    std::cout << "RealtimeServer 正在监听 0.0.0.0:" << m_port << std::endl;
}

void CServer::Run()
{
    std::array<epoll_event, kMaxEvents> events{};

    while (true) {
        const int ready_count = ::epoll_wait(m_epoll_fd, events.data(),
                                             static_cast<int>(events.size()), -1);
        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw MakeSystemError("等待 epoll 事件失败");
        }

        for (int index = 0; index < ready_count; ++index) {
            const int event_fd = events[index].data.fd;
            const std::uint32_t event_flags = events[index].events;

            if (event_fd == m_listen_fd) {
                if ((event_flags & (EPOLLERR | EPOLLHUP)) != 0U) {
                    throw std::runtime_error("监听 socket 发生异常");
                }
                AcceptClients();
                continue;
            }
            if (event_fd == m_timer_fd) {
                HandleTimer();
                continue;
            }

            // 监听 fd 和 timerfd 之外的事件都属于已经建立的客户端 Session。
            const auto session_it = m_client_fds.find(event_fd);
            if (session_it == m_client_fds.end()) {
                continue;
            }

            const std::shared_ptr<Session>& session = session_it->second;
            bool should_close = false;
            if ((event_flags & EPOLLIN) != 0U) {
                should_close = !session->HandleRead();
            }
            if (!should_close && (event_flags & EPOLLOUT) != 0U) {
                should_close = !session->HandleWrite();
            }
            if ((event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
                should_close = true;
            }

            if (should_close) {
                CloseClient(event_fd);
                continue;
            }

            std::uint32_t client_events = EPOLLIN | EPOLLRDHUP;
            if (session->HasPendingWrite()) {
                // 只有队列中有待发送数据时才监听 EPOLLOUT，避免空转。
                client_events |= EPOLLOUT;
            }
            UpdateEpollEvents(event_fd, client_events);
        }
    }
}

void CServer::AcceptClients()
{
    while (true) {
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
            AddToEpoll(client_fd, EPOLLIN | EPOLLRDHUP);
            // CServer 保存 Session 并统一管理 fd 的关闭时机。
            m_client_fds[client_fd] = std::make_shared<Session>(client_fd);
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

    // 消费定时器事件，并检查逻辑线程是否向 Session 写入了待发送数据。
    for (const auto& client : m_client_fds) {
        std::uint32_t client_events = EPOLLIN | EPOLLRDHUP;
        if (client.second->HasPendingWrite()) {
            // 逻辑线程写入发送队列后，在下一次定时器事件中补充可写监听。
            client_events |= EPOLLOUT;
        }
        UpdateEpollEvents(client.first, client_events);
    }

    // timerfd 只负责定时触发，会议超时清理由 LogicSystem 的工作线程串行处理。
    LogicSystem::GetInstance()->PostReconnectTimeoutCheck();
}

void CServer::CloseClient(int client_fd)
{
    const auto session_it = m_client_fds.find(client_fd);
    if (session_it == m_client_fds.end()) {
        return;
    }

    // 关闭 fd 前保留对应的 Session。LogicSystem 通过该对象身份清理会议连接，
    // 不依赖可能被系统复用的 fd 数字。
    const std::shared_ptr<Session> session = session_it->second;
    m_client_fds.erase(session_it);

    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    ::close(client_fd);

    // 未入会连接不涉及房间状态，无需唤醒逻辑线程。
    if (session->GetMeetingId() != 0) {
        LogicSystem::GetInstance()->PostSessionDisconnected(session);
    }
    std::cout << "客户端已断开，fd=" << client_fd << std::endl;
}


void CServer::AddToEpoll(int fd, std::uint32_t events)
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        throw MakeSystemError("注册 epoll 事件失败");
    }
}

void CServer::UpdateEpollEvents(int fd, std::uint32_t events)
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event) == -1) {
        throw MakeSystemError("更新 epoll 事件失败");
    }
}
