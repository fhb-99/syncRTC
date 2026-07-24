#pragma once

#include <functional>

enum ErrorCodes
{
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
    ERROR_PASSWORD_INVALID = 1006, // 密码无效，不匹配
    ERROR_TOKEN = 1007,
};

enum RequestID
{
    AUTH_LOGIN_REQUEST = 1010,
    AUTH_LOGIN_RESPONSE = 1011,
    ID_CREATE_MEETING_REQUEST = 1012,
    ID_CREATE_MEETING_RESPONSE = 1013,
    ID_PAST_MEETING_REQUEST = 1014,  // 过去历史会议的请求
    ID_PAST_MEETING_RESPONSE = 1015,
};


class Defer
{
public:
    Defer(std::function<void()> func) { m_func = func; }
    ~Defer() { m_func(); }
private:
    std::function<void()> m_func;
};