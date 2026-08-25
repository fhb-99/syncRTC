#pragma once

#include <memory>
#include <string>

class Session;

class CServer
{
public:
    explicit CServer(std::string socket_path);
    ~CServer();

    void Start();
    void Run();

private:
    void HandleSignal(const std::shared_ptr<Session>& session, const std::string& message);

    std::string m_socket_path;
    int m_listen_fd;
    bool m_started;
    bool m_bound;
};
