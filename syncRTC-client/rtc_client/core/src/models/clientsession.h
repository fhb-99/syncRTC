#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <QString>

// 保存当前进程已验证的登录信息，不负责凭据持久化。
class ClientSession
{
public:
    void setDeviceId(const QString &deviceId);
    void setSessionToken(const QString &sessionToken);

    QString getDeviceId() const;
    QString getSessionToken() const;

private:
    QString m_deviceId;
    QString m_sessionToken;
};

#endif // CLIENTSESSION_H
