#pragma once

#include <cstdint>
#include <mutex>
#include <string>

class Session
{
public:
    explicit Session(int fd);

    int GetFd() const;
    bool Receive(std::string& payload);
    bool Send(const std::string& payload);

private:
    // RealtimeServer 与 MediaServer 之间的内部连接 fd。
    int m_fd;
    // libdatachannel 的回调线程和 UDS 读取线程可能同时写入，发送必须保持完整帧顺序。
    std::mutex m_send_mutex;
};
