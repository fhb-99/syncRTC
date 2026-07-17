#ifndef AUTHCONTROLLER_H
#define AUTHCONTROLLER_H

#include <QObject>
#include <memory>

#include "../../network/httpmgr.h"
#include "LoginController.h"
#include "PasswordResetController.h"
#include "RegisterController.h"

class AuthController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(RegisterController* registercontroller READ GetRegisterControll CONSTANT)
    Q_PROPERTY(LoginController* loginController READ GetLoginControll CONSTANT)
    Q_PROPERTY(PasswordResetController* passwordResetController READ GetPasswordResetControll CONSTANT)

public:
    explicit AuthController(QObject *parent = nullptr);

    ~AuthController() = default;

    RegisterController * GetRegisterControll() const { return m_register.get(); }
    LoginController * GetLoginControll() const { return m_login.get(); }
    PasswordResetController * GetPasswordResetControll() const { return m_reset.get(); }

signals:

private:
    std::unique_ptr<RegisterController> m_register;
    std::unique_ptr<LoginController> m_login;
    std::unique_ptr<PasswordResetController> m_reset;

};

#endif // AUTHCONTROLLER_H
