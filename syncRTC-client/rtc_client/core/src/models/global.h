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
    ID_CREATE_MEETING_REQUEST = 1012,
    ID_CREATE_MEETING_RESPONSE = 1013,
    ID_PAST_MEETING_REQUEST = 1014,  // 过去历史会议的请求
    ID_PAST_MEETING_RESPONSE = 1015,
    ID_JOIN_MEETING_REQUEST = 1016,
    ID_JOIN_MEETING_RESPONSE = 1017,
    ID_START_MEETING_REQUEST = 1018,
    ID_START_MEETING_RESPONSE = 1019,
    ID_MEETING_STARTED = 1020,
    ID_LEAVE_MEETING_REQUEST = 1021,
    ID_LEAVE_MEETING_RESPONSE = 1022,
};

enum ErrorCodes
{
    SUCCESS = 0,
    ERROR_JSON = 1,
    ERROR_REDIS = 2,
    ERROR_MYSQL = 3,
    ERROR_NETWORK = 101,
    RPCFailed = 102,
    ERROR_PASSWORD = 103,
    ERROR_MEETING_NOT_FOUND = 104,
    ERROR_MEETING_STATUS = 105,
    ERROR_MEETING_FULL = 106,
    ERROR_MEETING_ACCESS = 107,
    ERROR_VARIFY_EXPIRED = 108, //验证码过期
    ERROR_VARIFYCODE = 109, //验证码错误
    ERROR_USER_EXIST = 110,       //用户已经存在
    ERROR_PASSWORD_INVALID = 111, // 密码无效，不匹配
    ERROR_TOKEN = 112,
    ERROR_SESSION_INVALID = 113,
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
