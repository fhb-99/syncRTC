#include "LoginController.h"

#include "../../network/httpmgr.h"
#include "../../network/tcpmgr.h"

#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QJsonDocument>
#include <QJsonObject>

LoginController::LoginController(QObject *parent)
    : QObject(parent)
{
    initHttpHandlers();

    connect(this, &LoginController::signal_connect_tcp,
            TcpMgr::GetInstance().get(), &TcpMgr::slot_tcp_connect);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_connect_success,
            this, &LoginController::slot_connect_success);
    connect(TcpMgr::GetInstance().get(), &TcpMgr::signal_login_failed,
            this, &LoginController::slot_login_failed);
}

void LoginController::LoginRequest(const QString &account, const QString &password)
{
    qDebug() << "Login......";
    if(!checkPasswordValid(password)) {
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
    m_handlers.insert(RequestID::ID_LOGIN_USER, [this](QJsonObject json){
        int error = json["error"].toInt();
        if(error != ErrorCodes::SUCCESS) {
            emit loginFailed("登录失败");
            qDebug() << "LOGIN FAILED ERROR " << error;
            return;
        }
        // 从GateServer服务获取对应信息，同时展示主界面
        auto username = json["username"].toString();
        auto email = json["email"].toString();
        m_server.host = json["host"].toString();
        m_server.port = json["port"].toString();
        m_server.uid = json["uid"].toInt();
        m_server.token = json["token"].toString();
        int expires_in = json["expires_in"].toInt();

        // 同时，与服务器建立tcp长连接
        emit signal_connect_tcp(m_server);
    });
}

bool LoginController::checkPasswordValid(const QString &password)
{
    if(password.length() < 6) return false;

    // 创建一个正则表达式对象，按照上述密码要求
    // 这个正则表达式解释：
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    bool match = regExp.match(password).hasMatch();
    return match;
}

void LoginController::slot_login_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error)
{
    if(error != ErrorCodes::SUCCESS) {
        emit loginFailed("网络异常");
        qDebug() << "NETWORK ERROR " << error;
        return;
    }

    // 解析json，将字节流转QJsonDocument，QJsonObject解析
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res);

    if(jsonDoc.isEmpty()) {
        emit loginFailed("数据异常");
        qDebug() << "JSON ANALYSIS ERROR";
        return;
    }

    if(!jsonDoc.isObject()) {
        emit loginFailed("数据异常");
        qDebug() << "JSON ANALYSIS ERROR";
        return;
    }

    if (!m_handlers.contains(reqID)) {
        qDebug() << "NO HANDLER FOR REQUEST ID" << reqID;
        return;
    }

    m_handlers[reqID](jsonDoc.object());
}

void LoginController::slot_connect_success(bool success)
{
    if(success){
        qDebug() << "聊天服务连接成功，正在登录...";
        QJsonObject jsonObj;
        jsonObj["uid"] = m_server.uid;
        jsonObj["token"] = m_server.token;

        QJsonDocument doc(jsonObj);
        QByteArray jsonData = doc.toJson(QJsonDocument::Indented);

        //发送tcp请求给chat server
        emit TcpMgr::GetInstance()->signal_send_data(RequestID::ID_MEETING_LOGIN, jsonData);

    }else{
        qDebug() << "LOGIN FAILED";
        emit loginFailed("网络错误");
    }
}

void LoginController::slot_login_failed(int error)
{
    QString result = QString("登录失败, err is %1")
                         .arg(error);
    emit loginFailed(result);
}
