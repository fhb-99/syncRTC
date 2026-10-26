#include "net/Session.h"
#include "service/LogicSystem.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#include <utility>
#include <charconv>
#include <json/json.h>

#include <sys/socket.h>

namespace {

constexpr std::size_t kFrameHeaderLength = sizeof(std::uint16_t) * 2;
constexpr std::size_t kIoBudget = 64 * 1024;
constexpr std::size_t kMaxPendingBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxPendingFrames = 1024;

} // namespace

Session::Session(int fd, std::shared_ptr<IoWakeup> wakeup, std::uint64_t token)
    : m_fd(fd),
      m_wakeup(std::move(wakeup)),
      m_token(token),
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

void Session::ClearMediaSignalId(std::uint64_t expected)
{
    m_media_signal_id.compare_exchange_strong(expected, 0);
}

bool Session::IsClosed() const
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    return m_closed || m_close_requested;
}

void Session::RequestClose()
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    m_close_requested = true;
    if (m_wakeup) m_wakeup->Notify(m_token);
}

bool Session::HandleRead()
{
    std::array<char, 4096> read_buffer{};

    // 当前使用 LT：按批次让出循环，内核仍可读时下一轮继续。
    std::size_t received = 0;
    while (received < kIoBudget) {
        const ssize_t read_length = ::recv(m_fd, read_buffer.data(), read_buffer.size(), 0);
        if (read_length > 0) {
            received += static_cast<std::size_t>(read_length);
            m_receive_buffer.append(read_buffer.data(), static_cast<std::size_t>(read_length));
            // 每次追加数据后立即尝试解帧，兼容半包和一次到达多个帧。
            ParseFrames();
            if (IsClosed()) return false;
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
    return true;
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
    if (m_closed || m_close_requested) {
        return false;
    }
    if (message.size() > kMaxPendingBytes - m_pending_bytes || m_pending_frames >= kMaxPendingFrames) {
        // 慢客户端不能无限积压；失效连接，避免静默丢失关键状态通知后继续。
        m_close_requested = true;
        if (m_wakeup) {
            m_wakeup->Notify(m_token);
        }
        return false;
    }
    m_pending_bytes += message.size();
    ++m_pending_frames;
    m_pending.push_back(std::move(message));
    if (m_wakeup && !m_wakeup->Notify(m_token)) {
        m_close_requested = true;
        return false;
    }
    return true;
}

bool Session::HandleWrite()
{
    {
        std::lock_guard<std::mutex> lock(m_send_mutex);
        if (m_closed || m_close_requested) {
            return false;
        }
        // 旧发送队列优先。未取走的提交会保持可写监听，不依赖再次 Notify。
        if (m_send_queue.empty()) {
            m_send_queue.swap(m_pending);
        }
    }
    std::size_t written = 0;
    while (!m_send_queue.empty() && written < kIoBudget) {
        auto& message = m_send_queue.front();
        const auto count = ::send(m_fd, message.data() + m_send_offset,
            std::min(kIoBudget - written, message.size() - m_send_offset), MSG_NOSIGNAL);
        if (count > 0) {
            m_send_offset += static_cast<std::size_t>(count);
            written += static_cast<std::size_t>(count);
            if (m_send_offset == message.size()) {
                {
                    std::lock_guard<std::mutex> lock(m_send_mutex);
                    m_pending_bytes -= message.size();
                    --m_pending_frames;
                }
                m_send_queue.pop_front();
                m_send_offset = 0;
            }
            continue;
        }
        if (count == -1 && errno == EINTR) {
            continue;
        }
        return count == -1 && (errno == EAGAIN || errno == EWOULDBLOCK);
    }
    return true;
}

bool Session::HasPendingWrite() const
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    return m_close_requested || m_pending_frames != 0;
}

void Session::Close()
{
    std::lock_guard<std::mutex> lock(m_send_mutex);
    m_closed = true;
    m_pending.clear();
    m_send_queue.clear();
    m_pending_bytes = 0;
    m_pending_frames = 0;
    m_send_offset = 0;
}

bool Session::ConnectionExpired(std::chrono::steady_clock::time_point now, bool allow_recheck)
{
    const bool expired = GetUserId() <= 0
        ? now - m_created_at >= std::chrono::seconds(30)
        : m_heartbeat_enabled && now - m_last_heartbeat >= std::chrono::seconds(20);
    if (!expired) return false;
    // I/O 长暂停后给一次有界复查机会；没有新的有效 PING 就不能反复延期。
    if (allow_recheck && !m_recheck_used) {
        m_recheck_used = true;
        m_recheck_until = now + std::chrono::seconds(3);
    }
    return !m_recheck_used || now >= m_recheck_until;
}

void Session::HandleHeartbeat(const std::string& payload)
{
    // 小包和频率限制先于 JSON 解析；心跳不访问业务状态或存储。
    if (GetUserId() <= 0 || payload.size() > 96) { RequestClose(); return; }
    const auto now = std::chrono::steady_clock::now();
    if (now - m_heartbeat_window >= std::chrono::seconds(1)) {
        m_heartbeat_window = now;
        m_heartbeat_count = 0;
    }
    if (++m_heartbeat_count > 4) { RequestClose(); return; }
    Json::Value value;
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;
    builder["allowComments"] = false;
    builder["failIfExtra"] = true;
    builder["rejectDupKeys"] = true;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    if (!reader->parse(payload.data(), payload.data() + payload.size(), &value, nullptr) ||
        !value.isObject()) { RequestClose(); return; }
    std::string reply = "{}";
    if (value.empty() && !m_sequenced_heartbeat) {
        // 兼容一期空对象协议；一旦采用序号模式，同一连接不再降级。
    } else {
        if (value.size() != 1 || !value["seq"].isString()) { RequestClose(); return; }
        const auto sequence = value["seq"].asString();
        std::uint64_t number = 0;
        const auto parsed = std::from_chars(sequence.data(), sequence.data() + sequence.size(), number);
        if (sequence.empty() || sequence.size() > 20 || sequence.front() == '0' ||
            parsed.ec != std::errc{} || parsed.ptr != sequence.data() + sequence.size()) {
            RequestClose();
            return;
        }
        if (number <= m_heartbeat_sequence) return; // 重放不刷新期限，也不产生额外回包。
        m_sequenced_heartbeat = true;
        m_heartbeat_sequence = number;
        reply = "{\"seq\":\"" + sequence + "\"}";
    }
    m_heartbeat_enabled = true;
    m_last_heartbeat = now;
    m_recheck_used = false;
    if (!Send(ID_HEARTBEAT_RESPONSE, reply)) RequestClose();
}

void Session::ParseFrames()
{
    while (m_receive_buffer.size() >= kFrameHeaderLength) {
        // 协议头依次为 request_id(2B) 和 payload_length(2B)，均为大端。
        const auto* header = reinterpret_cast<const unsigned char*>(m_receive_buffer.data());
        const std::uint16_t request_id =
            static_cast<std::uint16_t>((header[0] << 8U) | header[1]);
        // 内部收尾事件有预留接纳容量，不能允许客户端伪造内部事件绕过限流。
        if (request_id >= 0xFFF0) { RequestClose(); return; }
        const std::uint16_t payload_length =
            static_cast<std::uint16_t>((header[2] << 8U) | header[3]);
        const std::size_t frame_length = kFrameHeaderLength + payload_length;

        if (m_receive_buffer.size() < frame_length) {
            // payload 尚未收全，保留缓冲等待下一次可读事件。
            return;
        }

        // 心跳在创建业务节点之前短路，保留统一帧边界和发送 FIFO。
        if (request_id == ID_HEARTBEAT_REQUEST) {
            const auto payload = m_receive_buffer.substr(kFrameHeaderLength, payload_length);
            m_receive_buffer.erase(0, frame_length);
            HandleHeartbeat(payload);
            if (IsClosed()) return;
            continue;
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
