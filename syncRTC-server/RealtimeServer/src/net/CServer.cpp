#include "net/CServer.h"

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
    for (const int client_fd : m_client_fds) {
        ::close(client_fd);
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

            if ((event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0U) {
                CloseClient(event_fd);
            }
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
            // 当前只关心断开和异常事件，收到业务数据时不会调用 recv。
            AddToEpoll(client_fd, EPOLLRDHUP);
            m_client_fds.insert(client_fd);
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

    // 这里仅消费定时器事件，后续可在此加入心跳和空闲连接检查。
}

void CServer::CloseClient(int client_fd)
{
    if (m_client_fds.erase(client_fd) == 0U) {
        return;
    }

    ::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
    ::close(client_fd);
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
