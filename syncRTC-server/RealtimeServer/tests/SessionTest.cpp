#include "net/Session.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool SendAll(int fd, const char* data, std::size_t length)
{
    std::size_t sent_length = 0;
    while (sent_length < length) {
        const ssize_t sent = ::send(fd, data + sent_length, length - sent_length, 0);
        if (sent <= 0) {
            return false;
        }
        sent_length += static_cast<std::size_t>(sent);
    }
    return true;
}

bool Expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    int sockets[2]{};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        std::cerr << "创建 socketpair 失败：" << std::strerror(errno) << std::endl;
        return 1;
    }

    const int flags = ::fcntl(sockets[0], F_GETFL, 0);
    if (flags == -1 || ::fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "设置非阻塞 socket 失败：" << std::strerror(errno) << std::endl;
        ::close(sockets[0]);
        ::close(sockets[1]);
        return 1;
    }

    auto session = std::make_shared<Session>(sockets[0]);
    if (!Expect(session->GetUserId() == 0, "新连接不应带有用户身份") ||
        !Expect((session->SetUserId(42), session->GetUserId() == 42),
                "登录后未保存用户身份")) {
        ::close(sockets[0]);
        ::close(sockets[1]);
        return 1;
    }

    const std::array<char, 7> frame = {
        static_cast<char>(0x03), static_cast<char>(0xE9),
        static_cast<char>(0x00), static_cast<char>(0x03),
        'a', 'b', 'c'};

    if (!SendAll(sockets[1], frame.data(), 2) ||
        !Expect(session->HandleRead(), "读取分片头失败") ||
        !SendAll(sockets[1], frame.data() + 2, frame.size() - 2) ||
        !Expect(session->HandleRead(), "读取完整帧失败")) {
        ::close(sockets[0]);
        ::close(sockets[1]);
        return 1;
    }

    if (!Expect(session->Send(1002, "ok"), "加入发送队列失败") ||
        !Expect(session->HasPendingWrite(), "发送队列状态错误") ||
        !Expect(session->HandleWrite(), "发送数据失败") ||
        !Expect(!session->HasPendingWrite(), "发送后队列未清空")) {
        ::close(sockets[0]);
        ::close(sockets[1]);
        return 1;
    }

    std::array<char, 6> sent_frame{};
    if (::recv(sockets[1], sent_frame.data(), sent_frame.size(), MSG_WAITALL) !=
            static_cast<ssize_t>(sent_frame.size()) ||
        !Expect(static_cast<unsigned char>(sent_frame[0]) == 0x03 &&
                    static_cast<unsigned char>(sent_frame[1]) == 0xEA,
                "发送 request_id 字节序错误") ||
        !Expect(static_cast<unsigned char>(sent_frame[2]) == 0x00 &&
                    static_cast<unsigned char>(sent_frame[3]) == 0x02,
                "发送长度字节序错误") ||
        !Expect(std::string(sent_frame.data() + 4, 2) == "ok", "发送 payload 错误")) {
        ::close(sockets[0]);
        ::close(sockets[1]);
        return 1;
    }

    ::close(sockets[0]);
    ::close(sockets[1]);
    return 0;
}
