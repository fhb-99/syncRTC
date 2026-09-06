#include "clientsession.h"

void ClientSession::setDeviceId(const QString &deviceId)
{
    m_deviceId = deviceId;
}

void ClientSession::setSessionToken(const QString &sessionToken)
{
    m_sessionToken = sessionToken;
}

QString ClientSession::getDeviceId() const
{
    return m_deviceId;
}

QString ClientSession::getSessionToken() const
{
    return m_sessionToken;
}
