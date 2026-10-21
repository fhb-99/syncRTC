#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_set>

class Session : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(int fd);
    ~Session();

    int GetFd() const;
    bool IsStopped() const;
    // 仅由 UDS 读取线程调用：入队前登记，读取循环结束后用于投递断开通知。
    void RememberRoom(std::uint64_t meeting_id);
    const std::unordered_set<std::uint64_t>& GetMeetingIds() const;
    // 从 UDS 读取一条完整长度帧，并投递给 LogicSystem 管理的 room worker。
    // 返回 false 表示对端断开、读取失败，或本地已停止这条内部连接。
    bool HandleRead();
    // true 只表示完整帧已入队，不表示对端已收到；不等待 socket 可写。
    // 队列超限时停用连接，不能丢掉一条控制信令后继续发送后续信令。
    bool Send(const std::string& payload);
    // 停止收发并丢弃未发完的数据，等待收发退出后关闭 fd；允许重复调用。
    void Close();

private:
    bool Receive(std::string& payload);
    void SendLoop();
    // 只停止收发，不 join、不 close，发送线程遇到错误时也可以调用。
    void Stop();

    // Session 接管 fd；CServer 不再单独 close，避免旧回调写入已被复用的 fd。
    int m_fd;
    std::unordered_set<std::uint64_t> m_meeting_ids;
    // 入队、停止标志和 fd 关闭共用此锁；实际阻塞发送不持锁。
    mutable std::mutex m_send_mutex;
    std::condition_variable m_send_condition;
    std::queue<std::string> m_send_queue;
    // 包括正在发送的帧，避免出队后仍占内存的数据绕过容量限制。
    std::size_t m_pending_bytes = 0;
    std::size_t m_pending_frames = 0;
    bool m_stopping = false;
    // Close 先 shutdown 唤醒 Receive，再取得读取锁，保证 close 前没有在途 recv。
    std::mutex m_read_mutex;
    // 多个关闭请求只允许一个执行 join 和 close，不与发送队列锁混用。
    std::mutex m_close_mutex;
    std::thread m_send_thread;
};
