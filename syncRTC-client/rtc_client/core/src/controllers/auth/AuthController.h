#ifndef AUTHCONTROLLER_H
#define AUTHCONTROLLER_H

#include <QObject>

#include "../../network/httpmgr.h"
#include "LoginController.h"
#include "PasswordResetController.h"
#include "RegisterController.h"

class AuthController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(HttpMgr* httpMgr READ httpMgr WRITE setHttpMgr NOTIFY httpMgrChanged)
    Q_PROPERTY(LoginController* login READ login CONSTANT)
    Q_PROPERTY(RegisterController* registration READ registration CONSTANT)
    Q_PROPERTY(PasswordResetController* passwordReset READ passwordReset CONSTANT)

public:
    explicit AuthController(QObject *parent = nullptr);

    HttpMgr *httpMgr() const;
    void setHttpMgr(HttpMgr *httpMgr);

    LoginController *login();
    RegisterController *registration();
    PasswordResetController *passwordReset();

signals:
    void httpMgrChanged();

private:
    HttpMgr *m_httpMgr = nullptr;
    LoginController m_login;
    RegisterController m_registration;
    PasswordResetController m_passwordReset;
};

#endif // AUTHCONTROLLER_H
