#pragma once

#include <string>

class CServer
{
public:
    explicit CServer(std::string socket_path);
    ~CServer();

    void Start();
    void Run();

private:
    std::string m_socket_path;
    int m_listen_fd;
    bool m_started;
    bool m_bound;
};
