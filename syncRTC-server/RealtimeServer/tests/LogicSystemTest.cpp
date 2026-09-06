#include "net/Session.h"
#include "service/LogicSystem.h"

#include <memory>

#include <sys/socket.h>
#include <unistd.h>

int main()
{
    int sockets[2]{};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        return 1;
    }

    auto session = std::make_shared<Session>(sockets[0]);
    session->SetMeetingId(1001);
    // 断线事件由 CServer 投递；这里验证它能够安全进入 LogicSystem 的工作队列。
    LogicSystem::GetInstance()->PostSessionDisconnected(session);

    ::close(sockets[0]);
    ::close(sockets[1]);
    return 0;
}
