#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <vector>

// 业务线程只登记需要处理的连接身份；同一连接合并唤醒，不复制帧数据。
// 共享所有权使迟到的 Send 可以安全失败，不会写入已关闭且被复用的 eventfd。
class IoWakeup
{
public:
    IoWakeup();
    ~IoWakeup();
    IoWakeup(const IoWakeup&) = delete;
    IoWakeup& operator=(const IoWakeup&) = delete;

    int GetFd() const;
    bool Notify(std::uint64_t token);
    std::vector<std::uint64_t> Take();
    void RequestStop();
    bool Stopping() const;
    void Close();

private:
    void WakeLocked();
    mutable std::mutex m_mutex;
    int m_fd;
    bool m_stopping = false;
    std::unordered_set<std::uint64_t> m_pending;
    std::deque<std::uint64_t> m_ready;
};
