#ifndef PROFILECONTROLLER_H
#define PROFILECONTROLLER_H

#include <QObject>
#include <QJsonObject>

#include "../../models/currentuserstate.h"

class ProfileController : public QObject
{
    Q_OBJECT
public:
    // CurrentUserState 由 main.cpp 创建并注入，ProfileController 不拥有它
    explicit ProfileController(CurrentUserState *currentUser, QObject *parent = nullptr);

    // 仅负责把已校验的个人资料写入 QML 可观察状态
    bool applyProfile(const QJsonObject &json);

private:
    CurrentUserState *m_currentUser;
};

#endif // PROFILECONTROLLER_H
