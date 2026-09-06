#pragma once

#include <string>

struct UserData {
    int uid;
    std::string username;
    std::string email;
    std::string password;
};

struct UserInfo {
    int uid;
    std::string username;
    std::string email;
};

struct ContactInfo {
    int uid;
    std::string username;
    std::string email;
    std::string display_name;
    std::string alias;
    std::string remark;
    int relation_status;
};

struct ServerInfo
{
    std::string host;
    std::string port;
    std::string token;
    int uid;
};
