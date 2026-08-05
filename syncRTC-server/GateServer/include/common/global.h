#pragma once

#include <functional>

#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>


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
    ERROR_SESSION_INVALID = 1007, // 登录会话失效
};


enum RequestID 
{
    ID_GET_VARIFY_CODE = 1000,
    ID_REISTER_USER = 1001,
    ID_RESET_USER = 1002,
    ID_LOGIN_USER = 1003,
    ID_MEETING_LOGIN = 1004,
    ID_GET_CONTACTS = 1004,     // 获取联系人列表
    ID_SEARCH_CONTACTS = 1005,  // 搜索联系人
    ID_ADD_CONTACT = 1006,      // 单向添加联系人
    ID_DELETE_CONTACT = 1007,   // 单向删除联系人
};

enum Modules {
    LOGIN_MOD = 1,
    REGISTER_MOD = 2,
    RESET_MOD = 3
};
// RAII，保证资源安全释放

class Defer
{
public:
    Defer(std::function<void()> func) { m_func = func; }
    ~Defer() { m_func(); }
private:
    std::function<void()> m_func;
};
