#include "net/CServer.h"

#include "net/Session.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::runtime_error MakeSystemError(const std::string& action)
{
    return std::runtime_error(action + "：" + std::strerror(errno));
}

} // namespace

CServer::CServer(std::string socket_path)
    : m_socket_path(std::move(socket_path)),
      m_listen_fd(-1),
      m_started(false),
      m_bound(false)
{
}

CServer::~CServer()
{
    if (m_listen_fd != -1) {
        ::close(m_listen_fd);
    }
    if (m_bound) {
        ::unlink(m_socket_path.c_str());
    }
}

void CServer::Start()
{
    if (m_socket_path.empty()) {
        throw std::runtime_error("MediaServer 内部监听地址为空");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (m_socket_path.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("MediaServer 内部监听地址过长");
    }
    std::memcpy(address.sun_path, m_socket_path.data(), m_socket_path.size());

    m_listen_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_listen_fd == -1) {
        throw MakeSystemError("创建 MediaServer UDS 监听失败");
    }
    if (::unlink(m_socket_path.c_str()) == -1 && errno != ENOENT) {
        throw MakeSystemError("清理旧 MediaServer UDS 失败");
    }
    if (::bind(m_listen_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        throw MakeSystemError("绑定 MediaServer UDS 失败");
    }
    m_bound = true;
    if (::listen(m_listen_fd, SOMAXCONN) == -1) {
        throw MakeSystemError("开始监听 MediaServer UDS 失败");
    }

    m_started = true;
    std::cout << "MediaServer 内部信令地址：" << m_socket_path << std::endl;
}

void CServer::Run()
{
    if (!m_started) {
        throw std::runtime_error("MediaServer 尚未启动监听");
    }

    while (true) {
        const int client_fd = ::accept4(m_listen_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw MakeSystemError("接受 RealtimeServer UDS 连接失败");
        }

        auto session = std::make_shared<Session>(client_fd);
        // CServer 只负责 UDS 连接生命周期。完整信令帧由 Session 投递给
        // LogicSystem 的工作线程，避免网络读取线程直接处理 SDP 和房间状态。
        while (session->HandleRead()) {
        }
        ::close(client_fd);
    }
}
