#pragma once

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

struct ServerInfo
{
    std::string host;
    std::string port;
    std::string token;
    int uid;
};