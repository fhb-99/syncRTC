#include "net/Session.h"
#include "service/LogicSystem.h"

#include <array>
#include <cerrno>
#include <limits>
#include <utility>

#include <sys/socket.h>

namespace {

constexpr std::size_t kFrameHeaderLength = sizeof(std::uint16_t) * 2;

} // namespace

Session::Session(int fd)
    : m_fd(fd),
      m_user_id(0),
      m_meeting_id(0),
      m_media_signal_id(0),
      m_send_offset(0)
{
}

int Session::GetFd() const
{
    return m_fd;
}

void Session::SetUserId(int user_id)
{
    m_user_id = user_id;
}

int Session::GetUserId() const
{
    return m_user_id;
}

void Session::SetMeetingId(std::uint64_t meeting_id)
{
    m_meeting_id = meeting_id;
}

std::uint64_t Session::GetMeetingId() const
{
    return m_meeting_id;
}

void Session::SetMediaSignalId(std::uint64_t signal_id)
{
    m_media_signal_id = signal_id;
}

std::uint64_t Session::GetMediaSignalId() const
{
    return m_media_signal_id;
}

bool Session::HandleRead()
{
    std::array<char, 4096> read_buffer{};

    // 非阻塞 socket 需要一直读到 EAGAIN，避免遗漏本次 epoll 事件中的数据。
    while (true) {
        const ssize_t read_length = ::recv(m_fd, read_buffer.data(), read_buffer.size(), 0);
        if (read_length > 0) {
            m_receive_buffer.append(read_buffer.data(), static_cast<std::size_t>(read_length));
            // 每次追加数据后立即尝试解帧，兼容半包和一次到达多个帧。
            ParseFrames();
            continue;
        }

        if (read_length == 0) {
            // 对端已正常关闭写端，交由 CServer 移除该 Session。
            return false;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        return false;
    }
}

bool Session::Send(std::uint16_t request_id, const std::string& payload)
{
    // 协议长度字段只有 2 字节，单帧 payload 不能超过 65535 字节。
    if (payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    const auto payload_length = static_cast<std::uint16_t>(payload.size());
    std::string message(kFrameHeaderLength + payload.size(), '\0');
    // 手动写入大端帧头，与 Qt 客户端的 QDataStream::BigEndian 保持一致。
    message[0] = static_cast<char>((request_id >> 8U) & 0xFFU);
    message[1] = static_cast<char>(request_id & 0xFFU);
    message[2] = static_cast<char>((payload_length >> 8U) & 0xFFU);
    message[3] = static_cast<char>(payload_length & 0xFFU);
    message.replace(kFrameHeaderLength, payload.size(), payload);

    std::lock_guard<std::mutex> lock(m_send_mutex);
    m_send_queue.push_back(std::move(message));
    return true;
}

bool Session::HandleWrite()
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    while (!m_send_queue.empty()) {
        std::string& message = m_send_queue.front();
        // 非阻塞 send 可能只写出一部分，剩余部分由 m_send_offset 记录。
        const ssize_t sent_length = ::send(
            m_fd, message.data() + m_send_offset, message.size() - m_send_offset, MSG_NOSIGNAL);

        if (sent_length > 0) {
            m_send_offset += static_cast<std::size_t>(sent_length);
            if (m_send_offset == message.size()) {
                m_send_queue.pop_front();
                m_send_offset = 0;
            }
            continue;
        }

        if (sent_length == -1 && errno == EINTR) {
            continue;
        }
        if (sent_length == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        return false;
    }

    return true;
}

bool Session::HasPendingWrite() const
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    return !m_send_queue.empty();
}

void Session::ParseFrames()
{
    while (m_receive_buffer.size() >= kFrameHeaderLength) {
        // 协议头依次为 request_id(2B) 和 payload_length(2B)，均为大端。
        const auto* header = reinterpret_cast<const unsigned char*>(m_receive_buffer.data());
        const std::uint16_t request_id =
            static_cast<std::uint16_t>((header[0] << 8U) | header[1]);
        const std::uint16_t payload_length =
            static_cast<std::uint16_t>((header[2] << 8U) | header[3]);
        const std::size_t frame_length = kFrameHeaderLength + payload_length;

        if (m_receive_buffer.size() < frame_length) {
            // payload 尚未收全，保留缓冲等待下一次可读事件。
            return;
        }

        auto message = std::make_shared<LogicNode>();
        message->session = shared_from_this();
        message->id = request_id;
        message->message = m_receive_buffer.substr(kFrameHeaderLength, payload_length);
        m_receive_buffer.erase(0, frame_length);

        // Session 只负责解帧，后续业务处理交给 LogicSystem 的工作线程。
        LogicSystem::GetInstance()->PostMsgToQue(std::move(message));
    }
}
