#ifndef LOGINCONTROLLER_H
#define LOGINCONTROLLER_H

#include <QObject>
#include <QMap>

#include "../../models/Data.h"
#include "../../models/global.h"


class LoginController : public QObject
{
    Q_OBJECT

public:
    explicit LoginController(QObject *parent = nullptr);

    Q_INVOKABLE void LoginRequest(const QString& account, const QString& password);

private:
    QMap<RequestID, std::function<void(const QJsonObject&)>> m_handlers;

    void initHttpHandlers();

    bool checkPasswordValid(const QString& password);

    ServerInfo m_server;
signals:
    void loginSucceeded(const QString& username, const QString& email);
    void loginFailed(const QString& reason);
    void signal_connect_tcp(ServerInfo);
public slots:
    //  http
    void slot_login_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);
    // tcp
    void slot_connect_success(bool success);
    void slot_login_failed(int error);

};

#endif // LOGINCONTROLLER_H
