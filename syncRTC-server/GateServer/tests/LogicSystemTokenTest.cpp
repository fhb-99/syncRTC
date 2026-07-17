#include "service/LogicSystem.h"
#include "storage/RedisMgr.h"

#include <iostream>
#include <string>

int main()
{
    const std::string first = LogicSystem::GenerateToken();
    const std::string second = LogicSystem::GenerateToken();

    if (first.size() != 64 || second.size() != 64) {
        std::cerr << "token must contain 32 random bytes encoded as hex" << std::endl;
        return 1;
    }

    if (first == second) {
        std::cerr << "two generated tokens must differ" << std::endl;
        return 1;
    }

    const std::string key = "auth:session:" + first;
    if (!RedisMgr::GetInstance()->Set(key, "42", 60)) {
        std::cerr << "token session was not stored in Redis" << std::endl;
        return 1;
    }

    std::string uid;
    const bool found = RedisMgr::GetInstance()->Get(key, uid);
    RedisMgr::GetInstance()->Del(key);
    if (!found || uid != "42") {
        std::cerr << "token session was not read from Redis" << std::endl;
        return 1;
    }

    return 0;
}
