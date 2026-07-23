#ifndef GLOBAL_H
#define GLOBAL_H

#include <functional>
#include <QString>
#include <QDebug>

enum RequestID {
    ID_GET_VARIFY_CODE = 1000,
    ID_REISTER_USER = 1001,
    ID_RESET_USER = 1002,
    ID_LOGIN_USER = 1003,
    AUTH_LOGIN_REQUEST = 1010,
    AUTH_LOGIN_RESPONSE = 1011,
};

enum ErrorCodes {
    SUCCESS = 0,
    ERROR_JSON = 1,
    ERROR_REDIS = 2,
    ERROR_MYSQL = 3,
    ERROR_NETWORK = 101,
    RPCFailed = 102,
    ERROR_PASSWORD = 103,
    ERROR_VARIFY_EXPIRED = 1003, //验证码过期
    ERROR_VARIFYCODE = 1004, //验证码错误
    ERROR_USER_EXIST = 1005,       //用户已经存在
    ERROR_PASSWORD_INVALID = 1006, // 密码无效
    ERROR_SESSION_INVALID = 1007, // 登录会话失效
};

enum Modules {
    LOGIN_MOD = 1,
    REGISTER_MOD = 2,
    RESET_MOD = 3
};

// RAII，保证资源安全释放
class Defer {
public:
    Defer(std::function<void()> func) { m_func = func; }

    ~Defer() { m_func(); }
private:
    std::function<void()> m_func;
};

inline QString GateServer_URL = "";



#endif // GLOBAL_H
