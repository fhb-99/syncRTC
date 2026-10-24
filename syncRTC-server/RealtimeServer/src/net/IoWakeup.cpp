#include "net/IoWakeup.h"

#include <cerrno>
#include <stdexcept>
#include <sys/eventfd.h>
#include <unistd.h>

IoWakeup::IoWakeup() : m_fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC))
{
    if (m_fd == -1) {
        throw std::runtime_error("创建 I/O 唤醒 fd 失败");
    }
}

IoWakeup::~IoWakeup() { Close(); }

int IoWakeup::GetFd() const { return m_fd; }

void IoWakeup::WakeLocked()
{
    const std::uint64_t one = 1;
    while (::write(m_fd, &one, sizeof(one)) == -1 && errno == EINTR) {}
    // EAGAIN 表示已有未消费通知。fd 的关闭也在同一把锁下进行。
}

bool IoWakeup::Notify(std::uint64_t token)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping) {
        return false;
    }
    if (m_pending.insert(token).second) {
        m_ready.push_back(token);
        WakeLocked();
    }
    return true;
}

std::vector<std::uint64_t> IoWakeup::Take()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::uint64_t count;
    while (::read(m_fd, &count, sizeof(count)) == -1 && errno == EINTR) {}
    // 每轮最多推进 64 个连接，剩余连接保持 FIFO，并重新通知下一轮。
    std::vector<std::uint64_t> pending;
    while (!m_ready.empty() && pending.size() < 64) {
        pending.push_back(m_ready.front());
        m_pending.erase(m_ready.front());
        m_ready.pop_front();
    }
    if (!m_ready.empty()) {
        WakeLocked();
    }
    return pending;
}

void IoWakeup::RequestStop()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_stopping) {
        m_stopping = true;
        WakeLocked();
    }
}

bool IoWakeup::Stopping() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopping;
}

void IoWakeup::Close()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stopping = true;
    m_pending.clear();
    m_ready.clear();
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
}
