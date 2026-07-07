#include "AuthController.h"

AuthController::AuthController(QObject *parent)
    : QObject(parent),
      m_login(this),
      m_registration(this),
      m_passwordReset(this)
{
}

HttpMgr *AuthController::httpMgr() const
{
    return m_httpMgr;
}

void AuthController::setHttpMgr(HttpMgr *httpMgr)
{
    if (m_httpMgr == httpMgr) {
        return;
    }

    m_httpMgr = httpMgr;
    emit httpMgrChanged();
}

LoginController *AuthController::login()
{
    return &m_login;
}

RegisterController *AuthController::registration()
{
    return &m_registration;
}

PasswordResetController *AuthController::passwordReset()
{
    return &m_passwordReset;
}
