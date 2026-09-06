#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "config/ConfigMgr.h"
#include "net/CServer.h"

int main()
{
    try {
        const std::string socket_path = ConfigMgr::Init()["MediaServer"]["InternalSocketPath"];
        if (socket_path.empty()) {
            std::cerr << "MediaServer 配置错误：缺少 MediaServer.InternalSocketPath" << std::endl;
            return EXIT_FAILURE;
        }

        CServer server(socket_path);
        server.Start();
        server.Run();
    }
    catch (const std::exception& exception) {
        std::cerr << "MediaServer 启动失败：" << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
