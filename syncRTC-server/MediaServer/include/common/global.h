#pragma once

#include <functional>

enum ErrorCodes
{
    SUCCESS = 0,
    ERROR_JSON = 1,
    ERROR_NETWORK = 101,
    ERROR_MEDIA_ROOM = 201,
    ERROR_MEDIA_PEER = 202,
};

enum MediaSignalType
{
    MEDIA_SIGNAL_OFFER = 1,
    MEDIA_SIGNAL_CANDIDATE = 2,
};

class Defer
{
public:
    Defer(std::function<void()> func) { m_func = func; }
    ~Defer() { m_func(); }
private:
    std::function<void()> m_func;
};
