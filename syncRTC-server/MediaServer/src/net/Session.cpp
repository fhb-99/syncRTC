#include "net/Session.h"

#include "service/LogicSystem.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <utility>

#include <sys/socket.h>
#include <unistd.h>

namespace {

// 当前 UDS 只承载控制信令。同时限制字节数和帧数，防止慢对端导致无限积压。
constexpr std::size_t kMaxPendingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxPendingFrames = 1024;

bool SendAll(int fd, const char* data, std::size_t length)
{
    std::size_t sent_length = 0;
    while (sent_length < length) {
        const ssize_t sent = ::send(fd, data + sent_length, length - sent_length, MSG_NOSIGNAL);
        if (sent > 0) {
            sent_length += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == -1 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

bool ReceiveAll(int fd, char* data, std::size_t length)
{
    std::size_t received_length = 0;
    while (received_length < length) {
        const ssize_t received = ::recv(fd, data + received_length, length - received_length, 0);
        if (received > 0) {
            received_length += static_cast<std::size_t>(received);
            continue;
        }
        if (received == -1 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

Session::Session(int fd)
    : m_fd(fd)
{
    try {
        m_send_thread = std::thread(&Session::SendLoop, this);
    }
    catch (...) {
        ::close(m_fd);
        throw;
    }
}

Session::~Session()
{
    Close();
}

int Session::GetFd() const
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    return m_fd;
}

bool Session::IsStopped() const
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    return m_stopping;
}

void Session::RememberRoom(std::uint64_t meeting_id)
{
    m_meeting_ids.insert(meeting_id);
}

const std::unordered_set<std::uint64_t>& Session::GetMeetingIds() const
{
    return m_meeting_ids;
}

bool Session::HandleRead()
{
    std::string payload;
    if (!Receive(payload)) {
        Stop();
        return false;
    }

    // Session 只完成长度帧解包。LogicSystem 校验 JSON 后按 meeting_id
    // 投递到固定 room worker，具体媒体信令不会在 CServer 读取线程执行。
    LogicSystem::GetInstance()->PostMsgToQue(shared_from_this(), std::move(payload));
    return true;
}

bool Session::Receive(std::string& payload)
{
    std::lock_guard<std::mutex> read_lock(m_read_mutex);
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (m_stopping) {
            return false;
        }
    }

    std::array<unsigned char, sizeof(std::uint32_t)> header{};
    if (!ReceiveAll(m_fd, reinterpret_cast<char*>(header.data()), header.size())) {
        return false;
    }

    const std::uint32_t body_length =
        (static_cast<std::uint32_t>(header[0]) << 24U) |
        (static_cast<std::uint32_t>(header[1]) << 16U) |
        (static_cast<std::uint32_t>(header[2]) << 8U) |
        static_cast<std::uint32_t>(header[3]);
    payload.assign(body_length, '\0');
    return body_length == 0 || ReceiveAll(m_fd, payload.data(), payload.size());
}

bool Session::Send(const std::string& payload)
{
    std::unique_lock<std::mutex> lock(m_send_mutex);
    if (m_stopping) {
        return false;
    }

    // 容量也约束单帧长度，因此长度字段转换为 uint32_t 不会溢出。
    if (payload.size() > kMaxPendingBytes - sizeof(std::uint32_t) ||
        payload.size() + sizeof(std::uint32_t) > kMaxPendingBytes - m_pending_bytes ||
        m_pending_frames >= kMaxPendingFrames) {
        lock.unlock();
        Stop();
        std::cerr << "[uds-send-stop] reason=queue-limit" << std::endl;
        return false;
    }

    const auto body_length = static_cast<std::uint32_t>(payload.size());
    std::array<char, sizeof(std::uint32_t)> header{
        static_cast<char>((body_length >> 24U) & 0xFFU),
        static_cast<char>((body_length >> 16U) & 0xFFU),
        static_cast<char>((body_length >> 8U) & 0xFFU),
        static_cast<char>(body_length & 0xFFU)};

    std::string frame(header.data(), header.size());
    frame.append(payload);
    const std::size_t frame_size = frame.size();
    m_send_queue.push(std::move(frame));
    m_pending_bytes += frame_size;
    ++m_pending_frames;
    lock.unlock();
    m_send_condition.notify_one();
    return true;
}

void Session::SendLoop()
{
    while (true) {
        std::string frame;
        {
            std::unique_lock<std::mutex> lock(m_send_mutex);
            m_send_condition.wait(lock, [this]() {
                return m_stopping || !m_send_queue.empty();
            });
            if (m_stopping) {
                return;
            }
            frame = std::move(m_send_queue.front());
            m_send_queue.pop();
        }

        // 只有此线程写 socket，一帧完整写完才取下一帧，部分写入也不会串帧。
        // 不持队列锁；Close 的 shutdown 可以唤醒被慢对端阻塞的 send。
        if (!SendAll(m_fd, frame.data(), frame.size())) {
            Stop();
            return;
        }

        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (m_stopping) {
            return;
        }
        m_pending_bytes -= frame.size();
        --m_pending_frames;
    }
}

void Session::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (m_stopping) {
            return;
        }
        m_stopping = true;
        m_send_queue = {};
        m_pending_bytes = 0;
        m_pending_frames = 0;
        // shutdown 不释放 fd。先终止双向 I/O，避免 join 等待一个永远无法写完的帧。
        // 发送失败时也会唤醒 CServer 的读取循环，由它继续调用 Close 完成资源回收。
        ::shutdown(m_fd, SHUT_RDWR);
    }
    m_send_condition.notify_one();
}

void Session::Close()
{
    std::lock_guard<std::mutex> close_lock(m_close_mutex);
    Stop();
    if (m_send_thread.joinable()) {
        m_send_thread.join();
    }
    std::lock_guard<std::mutex> read_lock(m_read_mutex);
    std::lock_guard<std::mutex> lock(m_send_mutex);
    if (m_fd != -1) {
        ::close(m_fd);
        m_fd = -1;
    }
}
