#pragma once

#include "net/IoWakeup.h"
#include <cstddef>
#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

struct Frame
{
    // Qt 客户端发送的 2 字节请求号，已转换为主机字节序。
    std::uint16_t request_id;
    // 不含 4 字节帧头的原始 payload，暂不在 Session 内处理业务。
    std::string payload;
};

class Session : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(int fd, std::shared_ptr<IoWakeup> wakeup = {}, std::uint64_t token = 0);

    int GetFd() const;

    // TCP 登录校验成功后保存用户 ID，后续会议请求不再信任客户端传来的身份。
    void SetUserId(int user_id);
    int GetUserId() const;

    // 入会成功后记录当前会议，后续聊天/成员状态都从服务端会话状态推导。
    void SetMeetingId(std::uint64_t meeting_id);
    std::uint64_t GetMeetingId() const;

    // 一次 offer 协商对应一个内部 signal_id，后续 ICE candidate 沿用该标识返回原连接。
    void SetMediaSignalId(std::uint64_t signal_id);
    std::uint64_t GetMediaSignalId() const;
    void ClearMediaSignalId(std::uint64_t expected);

    // 读取并解析 Qt 客户端的 4 字节帧头，完整帧直接投递到 LogicSystem。
    bool HandleRead();

    // 组装协议帧并加入发送队列，实际发送由可写事件中的 HandleWrite 完成。
    bool Send(std::uint16_t request_id, const std::string& payload);
    bool HandleWrite();
    bool HasPendingWrite() const;
    // 仅由 I/O 线程失效连接；迟到的业务发送返回 false。
    void Close();
    void RequestClose();
    bool IsClosed() const;

private:
    void ParseFrames();

    // fd 的关闭由 CServer 统一负责，Session 只使用它进行收发。
    int m_fd;
    const std::shared_ptr<IoWakeup> m_wakeup;
    const std::uint64_t m_token;
    // 未登录连接保持为 0。
    std::atomic<int> m_user_id;
    // 未进入会议时保持为 0；离会/断线清理后也应重置为 0。
    std::atomic<std::uint64_t> m_meeting_id;
    // 最近一次媒体 offer 的内部关联标识，未协商时为 0。
    std::atomic<std::uint64_t> m_media_signal_id;
    // 半包会暂存在这里，直到凑齐一个完整帧。
    std::string m_receive_buffer;
    // 生产者只提交不可变完整帧；真正的发送队列和偏移仅由 I/O 访问。
    std::deque<std::string> m_pending;
    std::size_t m_pending_bytes = 0;
    std::size_t m_pending_frames = 0;
    bool m_closed = false;
    bool m_close_requested = false;
    // 非阻塞 send 未发完的数据按顺序保留在队列中。
    std::deque<std::string> m_send_queue;
    // 当前队首消息已经发送的字节数。
    std::size_t m_send_offset;
    // 只保护提交队列、总配额和连接失效状态，不持锁执行 socket I/O。
    mutable std::mutex m_send_mutex;
};
