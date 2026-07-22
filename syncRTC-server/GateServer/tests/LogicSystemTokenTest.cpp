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

    const std::string device_id = "test-device-id";
    const std::string key = "auth::session:" + first;
    if (!LogicSystem::SaveSession(first, 42, device_id)) {
        std::cerr << "token session was not stored in Redis" << std::endl;
        return 1;
    }

    int uid = 0;
    const bool found = LogicSystem::ValidateSession(first, device_id, uid);
    if (!found || uid != 42) {
        RedisMgr::GetInstance()->Del(key);
        std::cerr << "token session was not read from Redis" << std::endl;
        return 1;
    }

    if (LogicSystem::ValidateSession(first, "another-device", uid)) {
        RedisMgr::GetInstance()->Del(key);
        std::cerr << "token session accepted a different device" << std::endl;
        return 1;
    }

    RedisMgr::GetInstance()->Del(key);
    if (LogicSystem::ValidateSession(first, device_id, uid)) {
        std::cerr << "deleted token session was still accepted" << std::endl;
        return 1;
    }

    return 0;
}
