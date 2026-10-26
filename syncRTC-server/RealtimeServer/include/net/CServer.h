#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <map>
#include "net/Session.h"
#include "net/MediaSignalClient.h"

class CServer
{
    friend struct SingleIoTestAccess;
    friend struct HeartbeatTestAccess;
public:
    CServer(unsigned short port, MediaSignalClient& media_client);
    ~CServer();
    CServer(const CServer&) = delete;
    CServer& operator=(const CServer&) = delete;

    // main 必须在创建任何工作线程前屏蔽 SIGTERM/SIGINT，使其由此循环的 signalfd 接收。
    // 运行前创建监听 socket、epoll、timerfd、signalfd 与共享唤醒通道。
    void Start(const std::string& media_socket_path);
    // TCP 与 UDS 都由调用 Run 的线程处理；业务仍交给 LogicSystem。
    void Run();
    // 可跨线程请求退出；真正关闭 fd 在 Run 所属线程进行。
    void Stop();

private:
    void RunOnce(int max_wait_ms = -1);
    void Shutdown();
    void AcceptClients();
    void HandleTimer();
    void CheckHeartbeats();
    void HandleWakeup();
    void CloseClient(std::uint64_t token);
    void UpdateClient(std::uint64_t token, const std::shared_ptr<Session>& session);
    void AddToEpoll(int fd, std::uint32_t events, std::uint64_t token);

    unsigned short m_port;
    MediaSignalClient& m_media_client;
    int m_listen_fd = -1;
    int m_epoll_fd = -1;
    int m_timer_fd = -1;
    int m_signal_fd = -1;
    std::shared_ptr<IoWakeup> m_wakeup;
    // 身份不会随 fd 复用，唤醒与缓存的 epoll 事件都按此身份校验。
    std::uint64_t m_next_token = 5;
    // 有序 token 让扫描可跨轮继续，不保留会因插入/删除失效的迭代器。
    std::map<std::uint64_t, std::shared_ptr<Session>> m_sessions;
    std::uint64_t m_heartbeat_after = 0;
    std::uint64_t m_heartbeat_through = 0;
    bool m_heartbeat_scanning = false;
    bool m_heartbeat_recheck = false;
    std::chrono::steady_clock::time_point m_last_timer = std::chrono::steady_clock::now();
};
