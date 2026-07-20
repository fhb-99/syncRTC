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
    message->id = AUTH_LOGIN_REQUEST;
    message->message = "token";

    LogicSystem::GetInstance()->PostMsgToQue(message);

    ::close(sockets[0]);
    ::close(sockets[1]);
    return 0;
}
