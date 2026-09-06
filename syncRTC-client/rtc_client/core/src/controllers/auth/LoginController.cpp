#include "LoginController.h"

#include "../../models/clientsession.h"
#include "../../network/httpmgr.h"
#include "../../network/tcpmgr.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

LoginController::LoginController(ClientSession *clientSession, QObject *parent,
                                 const QString &credentialTarget)
    : QObject(parent),
      m_clientSession(clientSession),
      m_sessionStore(credentialTarget)
{
    initHttpHandlers();

    // 令牌只保留在控制器内存和 Windows 凭据库中，绝不暴露给 QML。
    m_hasRememberedSession = m_sessionStore.load(&m_rememberedAccount, &m_rememberedSessionToken);

    // LoginController 负责使用 GateServer 返回的地址建立 RealtimeServer TCP 连接
    connect(this, &LoginController::signal_connect_tcp,
            TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_connect_success,
            this, &LoginController::slot_connect_success);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_login_failed,
            this, &LoginController::slot_login_failed);
}

void LoginController::LoginRequest(const QString &account, const QString &password, bool rememberLogin)
{
    qDebug() << "Login request";
    if (!checkPasswordValid(password)) {
        emit loginFailed("密码格式不正确");
        return;
    }

    QJsonObject json;
    json["account"] = account;
    json["password"] = password;
    json["device_id"] = DeviceID();
    m_loginMode = LoginMode::Password;
    m_rememberLoginRequested = rememberLogin;
    m_loginAccount = account;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/login_user"),
                                            json,
                                            RequestID::ID_LOGIN_USER,
                                            Modules::LOGIN_MOD);
}

void LoginController::ResumeLoginRequest()
{
    if (!m_hasRememberedSession) {
        emit loginFailed("没有可用的登录状态");
        return;
    }

    QJsonObject json;
    json["session_token"] = m_rememberedSessionToken;
    json["device_id"] = DeviceID();
    m_loginMode = LoginMode::RememberedSession;
    m_rememberLoginRequested = true;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/login_user"),
                                            json,
                                            RequestID::ID_LOGIN_USER,
                                            Modules::LOGIN_MOD);
}

void LoginController::ForgetRememberedSession()
{
    if (!clearRememberedSession()) {
        emit rememberLoginWarning("无法清除已保存的登录状态");
    }
}

void LoginController::initHttpHandlers()
{
    m_handlers.insert(RequestID::ID_LOGIN_USER, [this](QJsonObject json) {
        const int error = json["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            if (m_loginMode == LoginMode::RememberedSession
                && error == ErrorCodes::ERROR_SESSION_INVALID) {
                if (!clearRememberedSession()) {
                    emit rememberLoginWarning("无法清除已失效的登录状态");
                }
                emit loginFailed("登录状态已过期，请重新登录");
                return;
            }

            emit loginFailed("登录失败");
            qDebug() << "Login request failed:" << error;
            return;
        }

        const QString sessionToken = json["session_token"].toString();
        if (sessionToken.isEmpty()) {
            emit loginFailed("登录响应缺少会话凭据");
            return;
        }

        m_server.email = json["email"].toString();
        m_server.host = json["host"].toString();
        m_server.port = json["port"].toString();
        m_server.uid = json["uid"].toInt();
        m_server.sessionToken = sessionToken;

        if (m_clientSession) {
            m_clientSession->setSessionToken(sessionToken);
        }

        if (m_loginMode == LoginMode::Password && !m_rememberLoginRequested) {
            if (!clearRememberedSession()) {
                emit rememberLoginWarning("无法清除已保存的登录状态");
            }
        } else {
            const QString account = m_loginMode == LoginMode::Password
                ? m_loginAccount
                : m_rememberedAccount;
            if (!saveRememberedSession(account, sessionToken)) {
                emit rememberLoginWarning("无法记住本次登录状态");
            }
        }

        // HTTP 登录成功后，由 LoginController 触发 RealtimeServer 的 TCP 连接。
        emit signal_connect_tcp(m_server);
    });
}

void LoginController::setDeviceID(const QString &deviceId)
{
    if (m_clientSession) {
        m_clientSession->setDeviceId(deviceId);
    }
}

QString LoginController::DeviceID() const
{
    return m_clientSession ? m_clientSession->getDeviceId() : QString();
}

bool LoginController::checkPasswordValid(const QString &password)
{
    if (password.length() < 6) {
        return false;
    }

    const QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    return regExp.match(password).hasMatch();
}

bool LoginController::saveRememberedSession(const QString &account, const QString &sessionToken)
{
    if (!m_sessionStore.save(account, sessionToken)) {
        return false;
    }

    const bool changed = !m_hasRememberedSession;
    m_rememberedAccount = account;
    m_rememberedSessionToken = sessionToken;
    m_hasRememberedSession = true;
    if (changed) {
        emit rememberedSessionChanged();
    }
    return true;
}

bool LoginController::clearRememberedSession()
{
    const bool cleared = m_sessionStore.clear();

    const bool changed = m_hasRememberedSession;
    m_rememberedAccount.clear();
    m_rememberedSessionToken.clear();
    m_hasRememberedSession = false;
    if (changed) {
        emit rememberedSessionChanged();
    }
    return cleared;
}

void LoginController::slot_login_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error)
{
    if (error != ErrorCodes::SUCCESS) {
        emit loginFailed("网络异常");
        qDebug() << "Login HTTP network error:" << error;
        return;
    }

    const QJsonDocument jsonDoc = QJsonDocument::fromJson(res);
    if (!jsonDoc.isObject()) {
        emit loginFailed("数据异常");
        qDebug() << "Login HTTP JSON parse error";
        return;
    }

    if (!m_handlers.contains(reqID)) {
        qDebug() << "No handler for request id:" << reqID;
        return;
    }

    m_handlers[reqID](jsonDoc.object());
}

void LoginController::slot_connect_success(bool success)
{
    if (!success) {
        emit loginFailed("TCP 连接失败");
        return;
    }

    QJsonObject json;
    json["uid"] = m_server.uid;
    json["token"] = m_server.sessionToken;
    json["email"] = m_server.email;

    // 连接成功后由 LoginController 发送 RealtimeServer 鉴权请求
    const QJsonDocument document(json);
    TcpMgr::GetInstance()->slot_send_data(
        AUTH_LOGIN_REQUEST, document.toJson(QJsonDocument::Compact));
}

void LoginController::slot_login_failed(int error)
{
    emit loginFailed(QString("登录失败，错误码：%1").arg(error));
}
