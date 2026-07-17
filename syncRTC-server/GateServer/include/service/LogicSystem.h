#pragma once

#include <functional>
#include <map>

#include "common/Singleton.h"
#include "common/global.h"
#include "net/HttpConnection.h"

class HttpConnection;

typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;

class LogicSystem : public Singleton<LogicSystem>
{
    friend class Singleton<LogicSystem>;
public:
    ~LogicSystem() {}
    static std::string HashPassword(const std::string& password);
    static bool VerifyPassword(const std::string& password,
                               const std::string& stored_hash);
    static std::string GenerateToken();
    bool HandleGet(std::string, std::shared_ptr<HttpConnection>);
    void RegisterGet(std::string, HttpHandler handler);

    bool HandlePost(std::string url, std::shared_ptr<HttpConnection>);
    void RegisterPost(std::string url, HttpHandler handler);
private:
    LogicSystem();
    std::map<std::string, HttpHandler> _post_handlers;
    std::map<std::string, HttpHandler> _get_handlers;
};
