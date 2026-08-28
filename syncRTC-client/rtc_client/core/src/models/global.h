#ifndef GLOBAL_H
#define GLOBAL_H

#include <functional>
#include <QString>
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

enum RequestID {
    ID_GET_VARIFY_CODE = 1000,
    ID_REISTER_USER = 1001,
    ID_RESET_USER = 1002,
    ID_LOGIN_USER = 1003,
    ID_GET_CONTACTS = 1004, // 获取联系人的在线状态
    ID_SEARCH_CONTACTS = 1005, // 搜索联系人
    ID_ADD_CONTACT = 1006,     // 单向添加联系人
    ID_DELETE_CONTACT = 1007,  // 单向删除联系人
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
    ID_END_MEETING_REQUEST = 1023,
    ID_END_MEETING_RESPONSE = 1024,
    ID_MEETING_ENDED = 1025,
    ID_SEND_MEETING_MESSAGE_REQUEST = 1026,
    ID_SEND_MEETING_MESSAGE_RESPONSE = 1027,
    ID_MEETING_MESSAGE_PUSH = 1028,
    ID_MEETING_MEMBER_JOINED = 1029,
    ID_MEETING_MEMBER_LEFT = 1030,
    ID_MEETING_MEMBER_RECONNECTING = 1031,
    ID_MEETING_MEMBER_RECONNECTED = 1032,
    ID_MEETING_MEMBER_TIMEOUT_LEFT = 1033,
    ID_GET_MEETING_GROUP_MESSAGES_REQUEST = 1034,
    ID_GET_MEETING_GROUP_MESSAGES_RESPONSE = 1035,
    ID_GET_MEETING_PRIVATE_MESSAGES_REQUEST = 1036,
    ID_GET_MEETING_PRIVATE_MESSAGES_RESPONSE = 1037,
    // WebRTC 媒体协商信令：offer/answer 描述媒体参数，candidate 描述可连接的网络地址。
    ID_MEDIA_OFFER_REQUEST = 1038,
    ID_MEDIA_CANDIDATE_REQUEST = 1039,
    ID_MEDIA_ANSWER_RESPONSE = 1040,
    ID_MEDIA_CANDIDATE_RESPONSE = 1041,
    ID_MEDIA_RENEGOTIATION_OFFER = 1042,
    ID_MEDIA_RENEGOTIATION_ANSWER_REQUEST = 1043,
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
    RESET_MOD = 3,
    CONTACTS_MOD = 4
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
