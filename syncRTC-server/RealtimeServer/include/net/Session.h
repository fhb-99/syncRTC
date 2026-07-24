#pragma once

#include <cstddef>
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
    explicit Session(int fd);

    int GetFd() const;

    // TCP 登录校验成功后保存用户 ID，后续会议请求不再信任客户端传来的身份。
    void SetUserId(int user_id);
    int GetUserId() const;

    // 读取并解析 Qt 客户端的 4 字节帧头，完整帧直接投递到 LogicSystem。
    bool HandleRead();

    // 组装协议帧并加入发送队列，实际发送由可写事件中的 HandleWrite 完成。
    bool Send(std::uint16_t request_id, const std::string& payload);
    bool HandleWrite();
    bool HasPendingWrite() const;

private:
    void ParseFrames();

    // fd 的关闭由 CServer 统一负责，Session 只使用它进行收发。
    int m_fd;
    // 未登录连接保持为 0。
    int m_user_id;
    // 半包会暂存在这里，直到凑齐一个完整帧。
    std::string m_receive_buffer;
    // 非阻塞 send 未发完的数据按顺序保留在队列中。
    std::deque<std::string> m_send_queue;
    // 当前队首消息已经发送的字节数。
    std::size_t m_send_offset;
    // 逻辑线程与 I/O 线程都会访问发送队列，需要保护队列状态。
    mutable std::mutex m_send_mutex;
};
