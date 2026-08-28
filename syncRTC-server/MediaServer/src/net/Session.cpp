#include "net/Session.h"

#include "service/LogicSystem.h"

#include <array>
#include <cerrno>
#include <limits>
#include <utility>

#include <sys/socket.h>

namespace {

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
}

int Session::GetFd() const
{
    return m_fd;
}

bool Session::HandleRead()
{
    std::string payload;
    if (!Receive(payload)) {
        return false;
    }

    auto message = std::make_shared<MediaSignalNode>();
    // Session 只完成长度帧解包。把来源连接和原始 JSON 一起交给逻辑线程，
    // 后续的 JSON 校验、SDP/candidate 处理都不会阻塞 CServer 的读取循环。
    message->session = shared_from_this();
    message->message = std::move(payload);
    LogicSystem::GetInstance()->PostMsgToQue(std::move(message));
    return true;
}

bool Session::Receive(std::string& payload)
{
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
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    const auto body_length = static_cast<std::uint32_t>(payload.size());
    std::array<char, sizeof(std::uint32_t)> header{
        static_cast<char>((body_length >> 24U) & 0xFFU),
        static_cast<char>((body_length >> 16U) & 0xFFU),
        static_cast<char>((body_length >> 8U) & 0xFFU),
        static_cast<char>(body_length & 0xFFU)};

    std::lock_guard<std::mutex> lock(m_send_mutex);
    return SendAll(m_fd, header.data(), header.size()) &&
           SendAll(m_fd, payload.data(), payload.size());
}
