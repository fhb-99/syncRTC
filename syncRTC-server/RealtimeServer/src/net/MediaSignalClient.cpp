#include "net/MediaSignalClient.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr std::size_t kMaxPendingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxPendingFrames = 1024;
constexpr std::size_t kHeaderSize = sizeof(std::uint32_t);
constexpr std::size_t kWriteBudget = 64 * 1024;
}

MediaSignalClient::MediaSignalClient(Callback callback) : m_callback(std::move(callback)) {}
MediaSignalClient::~MediaSignalClient() { Stop(); }

void MediaSignalClient::Start(const std::string& socket_path, int epoll_fd,
                              std::shared_ptr<IoWakeup> wakeup)
{
    sockaddr_un address{};
    if (socket_path.empty() || socket_path.size() >= sizeof(address.sun_path) ||
        epoll_fd < 0 || !wakeup) {
        throw std::invalid_argument("Media UDS 路径或 I/O 上下文无效");
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_started) {
        throw std::logic_error("Media UDS 已启动");
    }
    m_socket_path = socket_path;
    m_epoll_fd = epoll_fd;
    m_wakeup = std::move(wakeup);
    m_started = true;
    m_retry_ms = 1000;
    m_deadline = Clock::now();
}

bool MediaSignalClient::IsCurrent(std::uint64_t epoch) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_started && m_connected && m_epoch == epoch;
}

void MediaSignalClient::Invalidate(std::uint64_t epoch)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || epoch != m_epoch) return;
    m_connected = false;
    m_wakeup->Notify(kWakeToken);
}

bool MediaSignalClient::Send(std::uint64_t epoch, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_started || !m_connected || epoch != m_epoch) {
        return false;
    }
    if (message.size() > kMaxPendingBytes - kHeaderSize ||
        message.size() + kHeaderSize > kMaxPendingBytes - m_pending_bytes ||
        m_pending_frames >= kMaxPendingFrames) {
        // 与原实现一致：控制帧超额使本代立即失效，由 I/O 关闭并重连。
        m_connected = false;
        m_wakeup->Notify(kWakeToken);
        return false;
    }
    const auto size = static_cast<std::uint32_t>(message.size());
    std::string frame(kHeaderSize, '\0');
    frame[0] = static_cast<char>(size >> 24U);
    frame[1] = static_cast<char>(size >> 16U);
    frame[2] = static_cast<char>(size >> 8U);
    frame[3] = static_cast<char>(size);
    frame.append(message);
    m_pending_bytes += frame.size();
    ++m_pending_frames;
    m_frames.push(std::move(frame));
    if (!m_wakeup->Notify(kWakeToken)) {
        m_connected = false;
        return false;
    }
    return true;
}

void MediaSignalClient::Connect()
{
    m_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_fd == -1) {
        Disconnect();
        return;
    }
    ++m_socket_token;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, m_socket_path.c_str(), m_socket_path.size() + 1);
    const int result = ::connect(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result == -1 && errno != EINPROGRESS) {
        Disconnect();
        return;
    }
    m_connecting = result != 0;
    m_deadline = Clock::now() + std::chrono::seconds(1);
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP | (m_connecting ? EPOLLOUT : 0U);
    event.data.u64 = m_socket_token;
    if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_fd, &event) == -1) {
        throw std::runtime_error("注册 Media UDS 事件失败");
    }
    m_events = event.events;
    if (!m_connecting) {
        Connected();
    }
}

void MediaSignalClient::Connected()
{
    m_connecting = false;
    m_retry_ms = 1000;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_active_epoch = ++m_epoch;
        m_connected = true;
    }
    std::cout << "[media-uds-connected] epoch=" << m_active_epoch << std::endl;
    m_callback(Event::Connected, m_active_epoch, {});
}

void MediaSignalClient::Disconnect()
{
    const auto epoch = m_active_epoch;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connected = false;
        m_frames = {};
        m_pending_bytes = 0;
        m_pending_frames = 0;
    }
    if (m_fd != -1) {
        // 必须先移除再关闭，不能稍后按已被复用的 fd 删除另一个连接。
        ::epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, m_fd, nullptr);
        ::close(m_fd);
        m_fd = -1;
    }
    m_active_epoch = 0;
    m_connecting = false;
    m_events = 0;
    m_received.clear();
    m_sending.clear();
    m_sent = 0;
    m_deadline = Clock::now() + std::chrono::milliseconds(m_retry_ms);
    if (epoch == 0) {
        m_retry_ms = std::min(m_retry_ms * 2, 10000);
    } else {
        std::cout << "[media-uds-disconnected] epoch=" << epoch << std::endl;
        m_callback(Event::Disconnected, epoch, {});
    }
}

void MediaSignalClient::UpdateInterest()
{
    if (m_fd == -1) {
        return;
    }
    bool pending;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        pending = m_pending_frames != 0;
    }
    const std::uint32_t events = EPOLLIN | EPOLLRDHUP |
        ((m_connecting || pending) ? EPOLLOUT : 0U);
    if (events != m_events) {
        epoll_event event{};
        event.events = events;
        event.data.u64 = m_socket_token;
        if (::epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, m_fd, &event) == -1) {
            throw std::runtime_error("更新 Media UDS 事件失败");
        }
        m_events = events;
    }
}

void MediaSignalClient::Tick()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) {
            return;
        }
    }
    if ((m_active_epoch != 0 && !IsCurrent(m_active_epoch)) ||
        (m_connecting && Clock::now() >= m_deadline)) {
        Disconnect();
    }
    if (m_fd == -1 && Clock::now() >= m_deadline) {
        Connect();
    }
    UpdateInterest();
}

int MediaSignalClient::NextTimeoutMs() const
{
    if (m_fd != -1 && !m_connecting) {
        return -1;
    }
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(m_deadline - Clock::now()).count();
    return static_cast<int>(std::max<std::int64_t>(0, remaining));
}

void MediaSignalClient::HandleEvents(std::uint64_t token, std::uint32_t events)
{
    // 同一次 epoll_wait 返回的旧事件也必须校验，不能只比较 fd 数字。
    if (m_fd == -1 || token != m_socket_token) {
        return;
    }
    if (m_connecting) {
        int error = 0;
        socklen_t length = sizeof(error);
        if (!(events & EPOLLOUT) || ::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
            Disconnect();
            return;
        }
        Connected();
    }
    if (!IsCurrent(m_active_epoch) || (events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))) {
        Disconnect();
        return;
    }
    if (events & EPOLLIN) {
        std::array<char, 4096> buffer{};
        const auto count = ::recv(m_fd, buffer.data(), buffer.size(), 0);
        if (count == 0 || (count == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            Disconnect();
            return;
        }
        if (count > 0) {
            m_received.append(buffer.data(), static_cast<std::size_t>(count));
        }
        while (m_received.size() >= kHeaderSize) {
            const auto* header = reinterpret_cast<const unsigned char*>(m_received.data());
            const std::uint32_t size = (static_cast<std::uint32_t>(header[0]) << 24U) |
                (static_cast<std::uint32_t>(header[1]) << 16U) |
                (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
            if (size > kMaxPendingBytes - kHeaderSize) {
                Disconnect();
                return;
            }
            if (m_received.size() < kHeaderSize + size) {
                break;
            }
            m_callback(Event::Message, m_active_epoch, m_received.substr(kHeaderSize, size));
            m_received.erase(0, kHeaderSize + size);
        }
    }
    if (events & EPOLLOUT) {
        if (m_sending.empty()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_frames.empty()) {
                m_sending = std::move(m_frames.front());
                m_frames.pop();
                m_sent = 0;
            }
        }
        if (!m_sending.empty()) {
            const auto count = ::send(m_fd, m_sending.data() + m_sent,
                std::min(kWriteBudget, m_sending.size() - m_sent), MSG_NOSIGNAL);
            if (count == 0 || (count == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                Disconnect();
                return;
            }
            if (count > 0) {
                m_sent += static_cast<std::size_t>(count);
            }
            if (m_sent == m_sending.size()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pending_bytes -= m_sending.size();
                --m_pending_frames;
                m_sending.clear();
            }
        }
    }
    UpdateInterest();
}

void MediaSignalClient::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_started) {
            return;
        }
        m_started = false;
        m_connected = false;
    }
    Disconnect();
    m_epoll_fd = -1;
}
