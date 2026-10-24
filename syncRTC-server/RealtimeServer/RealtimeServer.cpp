#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>
#include <csignal>
#include <cstring>
#include <pthread.h>

#include "config/ConfigMgr.h"
#include "net/CServer.h"
#include "service/LogicSystem.h"

int main()
{
    // 必须先屏蔽，再创建业务/慢任务/gRPC 线程；所有线程继承屏蔽集，
    // SIGTERM/SIGINT 由 CServer 的 signalfd 转成普通 I/O 事件处理。
    sigset_t signals;
    ::sigemptyset(&signals);
    ::sigaddset(&signals, SIGTERM);
    ::sigaddset(&signals, SIGINT);
    const int signal_error = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    if (signal_error != 0) {
        std::cerr << "屏蔽退出信号失败：" << std::strerror(signal_error) << std::endl;
        return EXIT_FAILURE;
    }
    try {
        const std::string port_text = ConfigMgr::Init()["RealtimeServer"]["Port"];
        if (port_text.empty()) {
            std::cerr << "RealtimeServer 配置错误：缺少 RealtimeServer.Port" << std::endl;
            return EXIT_FAILURE;
        }

        std::size_t parsed_length = 0;
        unsigned long port_value = 0;
        try {
            port_value = std::stoul(port_text, &parsed_length);
        }
        catch (const std::exception&) {
            std::cerr << "RealtimeServer 配置错误：Port 必须在 1 到 65535 之间" << std::endl;
            return EXIT_FAILURE;
        }

        if (parsed_length != port_text.size() || port_value == 0 ||
            port_value > std::numeric_limits<unsigned short>::max()) {
            std::cerr << "RealtimeServer 配置错误：Port 必须在 1 到 65535 之间" << std::endl;
            return EXIT_FAILURE;
        }

        const std::string media_socket_path =
            ConfigMgr::Init()["MediaServer"]["InternalSocketPath"];
        if (media_socket_path.empty()) {
            std::cerr << "RealtimeServer 配置错误：缺少 MediaServer.InternalSocketPath" << std::endl;
            return EXIT_FAILURE;
        }

        // TCP 与 Media UDS 共用一个循环；Media 离线时仍接收客户端连接。
        auto logic = LogicSystem::GetInstance();
        // 声明在 server 前：正常返回和异常退出均先销毁 I/O，再排空业务。
        Defer drain_business([logic] { logic->Shutdown(); });
        CServer server(static_cast<unsigned short>(port_value), logic->GetMediaSignalClient());
        server.Start(media_socket_path);
        server.Run();
    }
    catch (const std::exception& exception) {
        std::cerr << "RealtimeServer 启动失败：" << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
