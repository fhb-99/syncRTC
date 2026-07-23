#include "realtimecontroller.h"

#include <QDebug>

RealtimeController::RealtimeController(CurrentUserState *currentUser, QObject *parent)
    : QObject{parent},
      m_currentUser(currentUser),
      m_profile(std::make_unique<ProfileController>(currentUser)),
      m_meeting(std::make_unique<MeetingController>())
{
    Q_ASSERT(m_currentUser);

    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_message_recv,
            this, &RealtimeController::slot_message_recv);
    initHandlers();
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

        // 两类初始化数据均写入状态后，再通知 QML 进入会议主界面。
        emit profileReady();
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
