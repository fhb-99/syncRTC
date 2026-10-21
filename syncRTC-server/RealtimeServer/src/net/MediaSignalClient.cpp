#include "net/MediaSignalClient.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
constexpr std::size_t kMaxPendingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxPendingFrames = 1024;
constexpr std::size_t kHeaderSize = sizeof(std::uint32_t);
} // namespace

MediaSignalClient::MediaSignalClient(Callback callback) : m_callback(std::move(callback)) {}

MediaSignalClient::~MediaSignalClient()
{
    Stop();
}

void MediaSignalClient::Start(const std::string& socket_path)
{
    sockaddr_un address{};
    if (socket_path.empty() || socket_path.size() >= sizeof(address.sun_path)) {
        throw std::invalid_argument("MediaServer UDS 路径为空或过长");
    }
    std::lock_guard<std::mutex> lifecycle_lock(m_lifecycle_mutex);
    if (m_thread.joinable()) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_wake_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wake_fd == -1) {
        throw std::runtime_error("创建 Media UDS 唤醒 fd 失败");
    }
    m_stopping = false;
    try {
        m_thread = std::thread(&MediaSignalClient::Run, this, socket_path);
    }
    catch (...) {
        m_stopping = true;
        ::close(m_wake_fd);
        m_wake_fd = -1;
        throw;
    }
}

void MediaSignalClient::Stop()
{
    std::lock_guard<std::mutex> lifecycle_lock(m_lifecycle_mutex);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
        m_connected = false;
        Wake();
    }
    // 不持队列锁等待线程；socket 的 close 始终由 I/O 线程执行。
    if (m_thread.joinable()) {
        m_thread.join();
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames = {};
    m_pending_bytes = 0;
    m_pending_frames = 0;
    if (m_wake_fd != -1) {
        ::close(m_wake_fd);
        m_wake_fd = -1;
    }
}

void MediaSignalClient::Wake()
{
    if (m_wake_fd != -1) {
        const std::uint64_t one = 1;
        while (::write(m_wake_fd, &one, sizeof(one)) == -1 && errno == EINTR) {}
        // EAGAIN 表示已经有未处理的唤醒，不需要重复写入。
    }
}

bool MediaSignalClient::Stopping() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopping;
}

bool MediaSignalClient::IsCurrent(std::uint64_t epoch) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connected && !m_stopping && m_epoch == epoch;
}

bool MediaSignalClient::Send(std::uint64_t epoch, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stopping || !m_connected || epoch != m_epoch) {
        return false;
    }
    if (message.size() > kMaxPendingBytes - kHeaderSize ||
        message.size() + kHeaderSize > kMaxPendingBytes - m_pending_bytes ||
        m_pending_frames >= kMaxPendingFrames) {
        // 控制信令不能丢一帧后继续协商。立即失效本代，唤醒 I/O 线程关闭并重连。
        m_connected = false;
        Wake();
        return false;
    }
    const auto length = static_cast<std::uint32_t>(message.size());
    std::string frame(kHeaderSize, '\0');
    frame[0] = static_cast<char>(length >> 24U);
    frame[1] = static_cast<char>(length >> 16U);
    frame[2] = static_cast<char>(length >> 8U);
    frame[3] = static_cast<char>(length);
    frame.append(message);
    m_pending_bytes += frame.size();
    ++m_pending_frames;
    m_frames.push(std::move(frame));
    Wake();
    return true;
}

short MediaSignalClient::Wait(int fd, short events, int timeout_ms)
{
    pollfd descriptors[2]{{m_wake_fd, POLLIN, 0}, {fd, events, 0}};
    int result;
    do {
        result = ::poll(descriptors, 2, timeout_ms);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        return POLLERR;
    }
    if (descriptors[0].revents & POLLIN) {
        std::uint64_t count;
        while (::read(m_wake_fd, &count, sizeof(count)) == -1 && errno == EINTR) {}
    }
    return descriptors[1].revents;
}

void MediaSignalClient::Run(std::string socket_path)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    int retry_ms = 1000;
    while (!Stopping()) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        bool connected = false;
        if (fd != -1) {
            const int result = ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
            connected = result == 0;
            if (result == -1 && errno == EINPROGRESS) {
                const auto events = Wait(fd, POLLOUT, 1000);
                int error = 0;
                socklen_t length = sizeof(error);
                connected = (events & POLLOUT) &&
                    ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
            }
        }
        if (!connected || Stopping()) {
            if (fd != -1) {
                ::close(fd);
            }
            if (Stopping()) {
                break;
            }
            Wait(-1, 0, retry_ms);
            retry_ms = std::min(retry_ms * 2, 10000);
            continue;
        }

        std::uint64_t epoch;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            epoch = ++m_epoch;
            m_connected = true;
        }
        retry_ms = 1000;
        std::cout << "[media-uds-connected] epoch=" << epoch << std::endl;
        m_callback(Event::Connected, epoch, {});
        // 每条连接使用独立解帧缓存、在途帧和偏移；断开后全部丢弃。
        std::string received;
        std::string sending;
        std::size_t sent = 0;
        while (IsCurrent(epoch)) {
            if (sending.empty()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_frames.empty()) {
                    sending = std::move(m_frames.front());
                    m_frames.pop();
                    sent = 0;
                }
            }
            const auto events = Wait(fd, POLLIN | (sending.empty() ? 0 : POLLOUT), -1);
            if (!IsCurrent(epoch) || (events & (POLLERR | POLLHUP | POLLNVAL))) {
                break;
            }
            if (events & POLLIN) {
                std::array<char, 4096> buffer{};
                const auto count = ::recv(fd, buffer.data(), buffer.size(), 0);
                if (count == 0 || (count == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    break;
                }
                if (count > 0) {
                    received.append(buffer.data(), static_cast<std::size_t>(count));
                }
                bool invalid_frame = false;
                while (received.size() >= kHeaderSize) {
                    const auto* header = reinterpret_cast<const unsigned char*>(received.data());
                    const std::uint32_t size = (static_cast<std::uint32_t>(header[0]) << 24U) |
                        (static_cast<std::uint32_t>(header[1]) << 16U) |
                        (static_cast<std::uint32_t>(header[2]) << 8U) | header[3];
                    if (size > kMaxPendingBytes - kHeaderSize) {
                        invalid_frame = true;
                        break;
                    }
                    if (received.size() < kHeaderSize + size) {
                        break;
                    }
                    m_callback(Event::Message, epoch, received.substr(kHeaderSize, size));
                    received.erase(0, kHeaderSize + size);
                }
                if (invalid_frame) {
                    break;
                }
            }
            if ((events & POLLOUT) && !sending.empty()) {
                const auto count = ::send(fd, sending.data() + sent, sending.size() - sent, MSG_NOSIGNAL);
                if (count == 0 || (count == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    break;
                }
                if (count > 0) {
                    sent += static_cast<std::size_t>(count);
                }
                if (sent == sending.size()) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_pending_bytes -= sending.size();
                    --m_pending_frames;
                    sending.clear();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connected = false;
            m_frames = {};
            m_pending_bytes = 0;
            m_pending_frames = 0;
        }
        ::close(fd);
        std::cout << "[media-uds-disconnected] epoch=" << epoch << std::endl;
        m_callback(Event::Disconnected, epoch, {});
        if (!Stopping()) {
            Wait(-1, 0, retry_ms);
        }
    }
}
