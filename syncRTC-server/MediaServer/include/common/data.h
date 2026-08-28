#pragma once

#include <cstdint>
#include <string>

// RealtimeServer 转给 MediaServer 的媒体信令上下文。
struct MediaSignalRequest
{
    // RealtimeServer 生成的协商关联标识，MediaServer 返回结果时必须原样带回。
    std::uint64_t signal_id = 0;
    std::uint64_t meeting_id = 0;
    int uid = 0;
    std::string signal_type;
    std::string sdp;
    std::string candidate;
    std::string mid;
};

// MediaServer 后续返回给 RealtimeServer 的媒体信令结果。
struct MediaSignalResponse
{
    std::uint64_t signal_id = 0;
    std::uint64_t meeting_id = 0;
    int uid = 0;
    std::string signal_type;
    std::string sdp;
    std::string candidate;
    std::string mid;
};
