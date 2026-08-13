#include "realtimecontroller.h"

#include <QDebug>
#include <QMetaObject>

RealtimeController::RealtimeController(CurrentUserState *currentUser, QObject *parent)
    : QObject{parent},
      m_currentUser(currentUser),
      m_profile(std::make_unique<ProfileController>(currentUser)),
      m_meeting(std::make_unique<MeetingController>()),
      m_chat(std::make_unique<ChatController>())
{
    Q_ASSERT(m_currentUser);

    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_message_recv,
            this, &RealtimeController::slot_message_recv);
    initHandlers();
}

void RealtimeController::setMediaController(QObject *mediaController)
{
    m_media = mediaController;
}

void RealtimeController::initHandlers()
{
    // 每个消息 ID 在这里绑定到对应业务控制器，路由函数本身不保存业务状态。
    m_handlers.insert(AUTH_LOGIN_RESPONSE, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            emit loginFailed(error);
            return;
        }

        // 兼容当前顶层资料字段，并允许服务端后续收敛为 profile 对象。
        const QJsonObject profileJson = json.contains("profile")
            ? json.value("profile").toObject()
            : json;
        const QJsonArray recentMeetingJson = json.value("meetings").toArray();

        if (!m_profile->applyProfile(profileJson)) {
            emit loginFailed(ErrorCodes::ERROR_JSON);
            return;
        }

        if (!m_meeting->applyRecentMeeting(recentMeetingJson)) {
            emit loginFailed(ErrorCodes::ERROR_JSON);
            return;
        }

        // 两类初始化数据均写入状态后，再通知 QML 进入主界面
        emit profileReady();
    });

    // 创建成功后服务返回最新 meetings 列表，交给控制器刷新 QML 模型
    m_handlers.insert(ID_CREATE_MEETING_RESPONSE, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            qWarning() << "Create meeting failed, error:" << error;
            return;
        }

        const QJsonValue meetingsValue = json.value("meetings");
        if (!meetingsValue.isArray() || !m_meeting->applyRecentMeeting(meetingsValue.toArray())) {
            qWarning() << "Invalid create meeting response";
        }
    });

    // 历史会议由服务端按距离当前时间由近到远排序，客户端保持服务端顺序展示。
    m_handlers.insert(ID_PAST_MEETING_RESPONSE, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            qWarning() << "Load history meetings failed, error:" << error;
            return;
        }

        const QJsonValue meetingsValue = json.value("meetings");
        if (!meetingsValue.isArray() || !m_meeting->applyHistoryMeetings(meetingsValue.toArray())) {
            qWarning() << "Invalid history meetings response";
        }
    });

    // 入会结果交给 MeetingController 校验并通知 QML；路由层不保存入会状态。
    m_handlers.insert(ID_JOIN_MEETING_RESPONSE, [this](const QJsonObject &json) {
        if (!m_meeting->applyJoinMeetingResponse(json)) {
            qWarning() << "Join meeting rejected or response invalid";
        }
    });

    m_handlers.insert(ID_START_MEETING_RESPONSE, [this](const QJsonObject &json) {
        if (!m_meeting->applyStartMeetingResponse(json)) {
            qWarning() << "Start meeting rejected or response invalid";
        }
    });

    m_handlers.insert(ID_MEETING_STARTED, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingStarted(json)) {
            qWarning() << "Meeting started notification invalid";
        }
    });

    m_handlers.insert(ID_LEAVE_MEETING_RESPONSE, [this](const QJsonObject &json) {
        if (!m_meeting->applyLeaveMeetingResponse(json)) {
            qWarning() << "Leave meeting rejected or response invalid";
        }
    });

    m_handlers.insert(ID_END_MEETING_RESPONSE, [this](const QJsonObject &json) {
        if (!m_meeting->applyEndMeetingResponse(json)) {
            qWarning() << "End meeting rejected or response invalid";
        }
    });

    m_handlers.insert(ID_MEETING_ENDED, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingEnded(json)) {
            qWarning() << "Meeting ended notification invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MEMBER_JOINED, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingMemberJoined(json)) {
            qWarning() << "Meeting member joined notification invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MEMBER_LEFT, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingMemberLeft(json)) {
            qWarning() << "Meeting member left notification invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MEMBER_RECONNECTING, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingMemberReconnecting(json)) {
            qWarning() << "Meeting member reconnecting notification invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MEMBER_RECONNECTED, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingMemberReconnected(json)) {
            qWarning() << "Meeting member reconnected notification invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MEMBER_TIMEOUT_LEFT, [this](const QJsonObject &json) {
        if (!m_meeting->applyMeetingMemberLeft(json)) {
            qWarning() << "Meeting member timeout-left notification invalid";
        }
    });

    // 聊天控制器只处理消息收发和展示，生命周期判断仍由 MeetingController 与服务端负责。
    m_handlers.insert(ID_SEND_MEETING_MESSAGE_RESPONSE, [this](const QJsonObject &json) {
        if (!m_chat->applySendMessageAck(json)) {
            qWarning() << "Meeting chat send acknowledgement invalid";
        }
    });

    m_handlers.insert(ID_MEETING_MESSAGE_PUSH, [this](const QJsonObject &json) {
        if (!m_chat->applyMessageReceived(json)) {
            qWarning() << "Meeting chat push invalid";
        }
    });

    m_handlers.insert(ID_GET_MEETING_GROUP_MESSAGES_RESPONSE, [this](const QJsonObject &json) {
        if (!m_chat->applyGroupHistoryResponse(json)) {
            qWarning() << "Meeting group history response invalid";
        }
    });

    m_handlers.insert(ID_GET_MEETING_PRIVATE_MESSAGES_RESPONSE, [this](const QJsonObject &json) {
        if (!m_chat->applyPrivateHistoryResponse(json)) {
            qWarning() << "Meeting private history response invalid";
        }
    });

    // 媒体 answer 仍由 RealtimeController 统一路由，但具体 SDP 处理交给 MediaController。
    m_handlers.insert(ID_MEDIA_ANSWER_RESPONSE, [this](const QJsonObject &json) {
        bool handled = false;
        if (m_media) {
            QMetaObject::invokeMethod(m_media, "applyMediaAnswer",
                                      Q_RETURN_ARG(bool, handled),
                                      Q_ARG(QJsonObject, json));
        }
        if (!handled) {
            qWarning() << "Media answer response invalid";
        }
    });

    // candidate 可能会分多次到达；这里同样只做转发，不在路由层解析 ICE 内容。
    m_handlers.insert(ID_MEDIA_CANDIDATE_RESPONSE, [this](const QJsonObject &json) {
        bool handled = false;
        if (m_media) {
            QMetaObject::invokeMethod(m_media, "applyMediaCandidate",
                                      Q_RETURN_ARG(bool, handled),
                                      Q_ARG(QJsonObject, json));
        }
        if (!handled) {
            qWarning() << "Media candidate response invalid";
        }
    });
}

void RealtimeController::slot_message_recv(RequestID reqID, QJsonObject json)
{
    const auto handler = m_handlers.constFind(reqID);
    if (handler == m_handlers.cend()) {
        qDebug() << "Unhandled realtime message id:" << reqID;
        return;
    }

    handler.value()(json);
}
