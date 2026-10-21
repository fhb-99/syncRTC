#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

// Realtime 到 Media 的单条 UDS 通道。只有 I/O 线程接触 socket，业务线程只入队。
class MediaSignalClient
{
public:
    enum class Event { Connected, Disconnected, Message };
    using Callback = std::function<void(Event, std::uint64_t, std::string)>;

    explicit MediaSignalClient(Callback callback);
    ~MediaSignalClient();
    void Start(const std::string& socket_path);
    void Stop();
    // true 仅表示已接受到该代次的发送队列。断线后丢弃旧帧，不跨连接重放。
    bool Send(std::uint64_t epoch, const std::string& message);
    bool IsCurrent(std::uint64_t epoch) const;

private:
    void Run(std::string socket_path);
    // eventfd 仅用于唤醒 poll；调用方持有 m_mutex，避免与 Stop 关闭它并发。
    void Wake();
    bool Stopping() const;
    short Wait(int fd, short events, int timeout_ms);

    Callback m_callback;
    std::mutex m_lifecycle_mutex;
    mutable std::mutex m_mutex;
    bool m_stopping = true;
    bool m_connected = false;
    std::uint64_t m_epoch = 0;
    int m_wake_fd = -1;
    std::thread m_thread;
    std::queue<std::string> m_frames;
    // 含 I/O 线程已经取走但尚未写完的帧。
    std::size_t m_pending_bytes = 0;
    std::size_t m_pending_frames = 0;
};
