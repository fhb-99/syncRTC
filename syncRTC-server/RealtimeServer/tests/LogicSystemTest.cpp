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
    auto message = std::make_shared<LogicNode>();
    message->session = session;
    // 这里只验证消息队列和工作线程，不依赖 Redis、MySQL 等外部服务。
    message->id = 0xFFFF;
    message->message = "token";

    LogicSystem::GetInstance()->PostMsgToQue(message);

    ::close(sockets[0]);
    ::close(sockets[1]);
    return 0;
}
