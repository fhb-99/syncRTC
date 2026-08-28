#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

class Session : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(int fd);

    int GetFd() const;
    // 从 UDS 读取一条完整长度帧，并投递给 LogicSystem 的工作线程。
    // 返回 false 表示 RealtimeServer 已关闭这条内部连接。
    bool HandleRead();
    bool Send(const std::string& payload);

private:
    bool Receive(std::string& payload);

    // RealtimeServer 与 MediaServer 之间的内部连接 fd。
    int m_fd;
    // libdatachannel 的回调线程和 UDS 读取线程可能同时写入，发送必须保持完整帧顺序。
    std::mutex m_send_mutex;
};
