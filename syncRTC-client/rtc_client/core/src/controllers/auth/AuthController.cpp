#include "AuthController.h"

AuthController::AuthController(ClientSession *clientSession, QObject *parent)
    : QObject(parent)
{
    m_register = std::make_unique<RegisterController>();
    m_login = std::make_unique<LoginController>(clientSession);
    m_reset = std::make_unique<PasswordResetController>();

    connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_register_mod_finish,
            m_register.get(), &RegisterController::slot_register_mod_finish);

    connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_reset_mod_finish,
            m_reset.get(), &PasswordResetController::slot_reset_mod_finish);

    connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
            m_login.get(), &LoginController::slot_login_mod_finish);
}
