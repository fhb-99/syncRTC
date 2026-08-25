#include "net/CServer.h"

#include "common/data.h"
#include "net/Session.h"
#include "service/LogicSystem.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <json/json.h>

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
        std::string message;
        while (session->Receive(message)) {
            HandleSignal(session, message);
        }
        ::close(client_fd);
    }
}

void CServer::HandleSignal(const std::shared_ptr<Session>& session, const std::string& message)
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(message, root) || !root.isObject() ||
        !root["signal_id"].isUInt64() || !root["meeting_id"].isUInt64() ||
        !root["uid"].isInt() || !root["signal_type"].isString()) {
        return;
    }

    MediaSignalRequest request;
    request.signal_id = root["signal_id"].asUInt64();
    request.meeting_id = root["meeting_id"].asUInt64();
    request.uid = root["uid"].asInt();
    request.signal_type = root["signal_type"].asString();
    if (request.signal_type == "offer" && root["sdp"].isString()) {
        request.sdp = root["sdp"].asString();
        LogicSystem::GetInstance()->HandleOffer(session, request);
        return;
    }
    if (request.signal_type == "candidate" && root["candidate"].isString() && root["mid"].isString()) {
        request.candidate = root["candidate"].asString();
        request.mid = root["mid"].asString();
        LogicSystem::GetInstance()->HandleCandidate(session, request);
    }
}
