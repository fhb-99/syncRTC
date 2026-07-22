#include "realtimecontroller.h"

#include <QDebug>

RealtimeController::RealtimeController(CurrentUserState *currentUser, QObject *parent)
    : QObject{parent},
      m_currentUser(currentUser),
      m_profile(std::make_unique<ProfileController>(currentUser))
{
    Q_ASSERT(m_currentUser);

    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_message_recv,
            this, &RealtimeController::slot_message_recv);
    initHandlers();
}

void RealtimeController::initHandlers()
{
    // 每个消息 ID 在这里绑定到对应业务控制器，路由函数本身不包含业务分支
    m_handlers.insert(AUTH_LOGIN_RESPONSE, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            emit loginFailed(error);
            return;
        }

        if (!m_profile->applyProfile(json)) {
            emit loginFailed(ErrorCodes::ERROR_JSON);
            return;
        }

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
