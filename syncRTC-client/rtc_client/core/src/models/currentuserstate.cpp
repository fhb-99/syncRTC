#include "currentuserstate.h"

CurrentUserState::CurrentUserState(QObject *parent)
    : QObject{parent}
{

}

int CurrentUserState::uid() const
{
    return m_uid;
}

QString CurrentUserState::username() const
{
    return m_username;
}

QString CurrentUserState::email() const
{
    return m_email;
}

void CurrentUserState::setUid(int uid)
{
    if (m_uid == uid) {
        return;
    }

    m_uid = uid;
    emit uidChanged();
}

void CurrentUserState::setEmail(const QString &email)
{
    if (m_email == email) {
        return;
    }

    m_email = email;
    emit emailChanged();
}

void CurrentUserState::setUsername(const QString &username)
{
    if (m_username == username) {
        return;
    }

    m_username = username;
    emit usernameChanged();
}
