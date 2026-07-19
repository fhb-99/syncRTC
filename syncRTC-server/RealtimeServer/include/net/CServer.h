#pragma once

#include <cstdint>
#include <unordered_set>

class CServer
{
public:
    explicit CServer(unsigned short port);
    ~CServer();

    CServer(const CServer&) = delete;
    CServer& operator=(const CServer&) = delete;

    // 创建监听 socket、epoll 和 timerfd。
    void Start();

    // 只处理连接接入、连接断开和定时器事件，不处理客户端业务数据。
    void Run();

private:
    void AcceptClients();
    void HandleTimer();
    void CloseClient(int client_fd);
    void AddToEpoll(int fd, std::uint32_t events);

    unsigned short m_port;
    int m_listen_fd;
    int m_epoll_fd;
    int m_timer_fd;
    std::unordered_set<int> m_client_fds;
};
