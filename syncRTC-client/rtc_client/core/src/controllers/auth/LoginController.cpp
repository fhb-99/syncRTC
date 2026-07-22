#include "LoginController.h"

#include "../../network/httpmgr.h"
#include "../../network/tcpmgr.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

LoginController::LoginController(QObject *parent)
    : QObject(parent)
{
    initHttpHandlers();

    // LoginController 负责使用 GateServer 返回的地址建立 RealtimeServer TCP 连接
    connect(this, &LoginController::signal_connect_tcp,
            TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_connect_success,
            this, &LoginController::slot_connect_success);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_login_failed,
            this, &LoginController::slot_login_failed);
}

void LoginController::LoginRequest(const QString &account, const QString &password)
{
    qDebug() << "Login request";
    if (!checkPasswordValid(password)) {
        emit loginFailed("密码格式不正确");
        return;
    }

    QJsonObject json;
    json["account"] = account;
    json["password"] = password;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/login_user"),
                                            json,
                                            RequestID::ID_LOGIN_USER,
                                            Modules::LOGIN_MOD);
}

void LoginController::initHttpHandlers()
{
    m_handlers.insert(RequestID::ID_LOGIN_USER, [this](QJsonObject json) {
        const int error = json["error"].toInt();
        if (error != ErrorCodes::SUCCESS) {
            emit loginFailed("登录失败");
            qDebug() << "Login request failed:" << error;
            return;
        }

        m_server.email = json["email"].toString();
        m_server.host = json["host"].toString();
        m_server.port = json["port"].toString();
        m_server.uid = json["uid"].toInt();
        m_server.token = json["token"].toString();

        // HTTP 登录成功后，由 LoginController 触发 RealtimeServer 的 TCP 连接。
        emit signal_connect_tcp(m_server);
    });
}

bool LoginController::checkPasswordValid(const QString &password)
{
    if (password.length() < 6) {
        return false;
    }

    const QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    return regExp.match(password).hasMatch();
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
    json["token"] = m_server.token;
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
