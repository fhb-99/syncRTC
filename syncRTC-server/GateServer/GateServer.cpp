#include <iostream>

#include "common/global.h"
#include "config/ConfigMgr.h"
#include "net/CServer.h"

int main()
{
    auto& config = ConfigMgr::Init();
    std::string port_str = config["GateServer"]["Port"];
    unsigned short port = static_cast<unsigned short>(atoi(port_str.c_str()));

    try {
        net::io_context  ioc{1};
        boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& error, int signal_number){
            if(error) {
                return;
            }
            ioc.stop();
        });

        std::make_shared<CServer>(ioc, port)->start();
        ioc.run();
    }
    catch(const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return 0;
}