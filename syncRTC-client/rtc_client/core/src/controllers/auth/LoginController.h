#ifndef LOGINCONTROLLER_H
#define LOGINCONTROLLER_H

#include <QObject>
#include <QMap>

#include "../../models/Data.h"
#include "../../models/global.h"
#include "../../models/sessioncredentialstore.h"

class ClientSession;

class LoginController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasRememberedSession READ hasRememberedSession NOTIFY rememberedSessionChanged)

public:
    explicit LoginController(ClientSession *clientSession,
                             QObject *parent = nullptr,
                             const QString &credentialTarget = SessionCredentialStore::defaultTargetName());

    Q_INVOKABLE void LoginRequest(const QString& account, const QString& password, bool rememberLogin);
    Q_INVOKABLE void ResumeLoginRequest();
    Q_INVOKABLE void ForgetRememberedSession();

    void setDeviceID(const QString& deviceId);
    QString DeviceID() const;
    bool hasRememberedSession() const { return m_hasRememberedSession; }

private:
    enum class LoginMode {
        Password,
        RememberedSession,
    };

    QMap<RequestID, std::function<void(const QJsonObject&)>> m_handlers;

    void initHttpHandlers();

    bool checkPasswordValid(const QString& password);
    bool saveRememberedSession(const QString &account, const QString &sessionToken);
    bool clearRememberedSession();

    ServerInfo m_server;

    ClientSession *m_clientSession = nullptr;
    QString m_loginAccount;
    QString m_rememberedAccount;
    QString m_rememberedSessionToken;
    SessionCredentialStore m_sessionStore;
    LoginMode m_loginMode = LoginMode::Password;
    bool m_rememberLoginRequested = false;
    bool m_hasRememberedSession = false;
signals:
    void loginSucceeded(const QString& username, const QString& email);
    void loginFailed(const QString& reason);
    void rememberedSessionChanged();
    void rememberLoginWarning(const QString &reason);
    void signal_connect_tcp(ServerInfo);
public slots:
    //  http
    void slot_login_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);
    // tcp
    void slot_connect_success(bool success);
    void slot_login_failed(int error);

};

#endif // LOGINCONTROLLER_H
