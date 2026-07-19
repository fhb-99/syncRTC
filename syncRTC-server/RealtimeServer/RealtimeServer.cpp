#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string>

#include "config/ConfigMgr.h"
#include "net/CServer.h"

int main()
{
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

        CServer server(static_cast<unsigned short>(port_value));
        server.Start();
        server.Run();
    }
    catch (const std::exception& exception) {
        std::cerr << "RealtimeServer 启动失败：" << exception.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}
