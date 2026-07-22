#include "profilecontroller.h"

#include <QDebug>

ProfileController::ProfileController(CurrentUserState *currentUser, QObject *parent)
    : QObject{parent},
      m_currentUser(currentUser)
{
    Q_ASSERT(m_currentUser);
}

bool ProfileController::applyProfile(const QJsonObject &json)
{
    const QString email = json.value("email").toString().trimmed();
    const QString username = json.value("username").toString().trimmed();

    if (email.isEmpty() || username.isEmpty()) {
        qDebug() << "用户资料信息缺少";
        return false;
    }

    m_currentUser->setUsername(username);
    m_currentUser->setEmail(email);
    return true;
}
