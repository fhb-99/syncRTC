#pragma once

#include "net/IoWakeup.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

// UDS 状态机由 CServer 的同一个 epoll 循环驱动，不再创建线程。
class MediaSignalClient
{
public:
    enum class Event { Connected, Disconnected, Message };
    using Callback = std::function<void(Event, std::uint64_t, std::string)>;
    static constexpr std::uint64_t kWakeToken = 1;
    static constexpr std::uint64_t kSocketTokenBit = std::uint64_t{1} << 63U;

    explicit MediaSignalClient(Callback callback);
    ~MediaSignalClient();
    // Start 在循环运行前调用；其余驱动方法及 Stop 仅在 I/O 线程调用。
    void Start(const std::string& socket_path, int epoll_fd, std::shared_ptr<IoWakeup> wakeup);
    void Tick();
    void HandleEvents(std::uint64_t token, std::uint32_t events);
    int NextTimeoutMs() const;
    void Stop();
    // 仅这两个接口允许业务线程调用；true 不代表对端已处理。
    bool Send(std::uint64_t epoch, const std::string& message);
    bool IsCurrent(std::uint64_t epoch) const;
    // 业务入口过载时失效本代；关闭和重连仍由唯一 I/O 线程执行。
    void Invalidate(std::uint64_t epoch);

private:
    using Clock = std::chrono::steady_clock;
    void Connect();
    void Connected();
    void Disconnect();
    void UpdateInterest();

    Callback m_callback;
    mutable std::mutex m_mutex;
    bool m_started = false;
    bool m_connected = false;
    std::uint64_t m_epoch = 0;
    std::shared_ptr<IoWakeup> m_wakeup;
    std::queue<std::string> m_frames;
    // 包含 I/O 已取走但尚未完整写出的帧，不能在出队时释放配额。
    std::size_t m_pending_bytes = 0;
    std::size_t m_pending_frames = 0;

    // 下列状态只有 I/O 线程访问。
    int m_epoll_fd = -1;
    int m_fd = -1;
    std::uint64_t m_socket_token = kSocketTokenBit;
    std::uint64_t m_active_epoch = 0;
    std::uint32_t m_events = 0;
    bool m_connecting = false;
    int m_retry_ms = 1000;
    Clock::time_point m_deadline;
    std::string m_socket_path;
    std::string m_received;
    std::string m_sending;
    std::size_t m_sent = 0;
};
