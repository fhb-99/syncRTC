#include "RegisterController.h"

#include "../../network/httpmgr.h"

#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QJsonDocument>
#include <QJsonObject>

RegisterController::RegisterController(QObject *parent)
    : QObject(parent)
{
    // 注册对应的http处理，信号来了，可以直接执行相关函数
    initHttpHandlers();
}

void RegisterController::GetVarifyCodeAsync(const QString& email)
{
    qDebug() << "Get Varify Code Button is Clicked";
    // 先做本地格式检查，格式不对直接返回失败
    if(!checkEmailValid(email)) {
        emit verifyCodeFailed("邮箱地址不正确");
        qDebug() << "邮箱地址不正确";
        return;
    }
    // 后续调用httpmgr进行网络通信，异步
    QJsonObject jsonObj;
    jsonObj["email"] = email;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/get_varifycode"),
                                            jsonObj,
                                            RequestID::ID_GET_VARIFY_CODE,
                                            Modules::REGISTER_MOD);
}

void RegisterController::RegisterRequest(const QString& username, const QString& email,
                                         const QString& varifycode, const QString& password,
                                         const QString& comfirm)
{
    if(!checkEmailValid(email)) {
        emit registerFailed("邮箱地址不正确");
        qDebug() << "邮箱地址不正确";
        return;
    }

    if(!checkPasswordValid(password))
    {
        emit registerFailed("密码格式不正确");
        qDebug() << "密码格式不正确";
        return;
    }

    if(password != comfirm) {
        emit registerFailed("两次输入的密码不匹配");
        return;
    }

    QJsonObject jsonObj;
    UserData user;
    user.username = username;
    user.email = email;
    user.password = password;
    jsonObj["UserData"] = user.toJson();
    jsonObj["VarifyCode"] = varifycode;
    jsonObj["Comfirm"] = comfirm;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/register_user"),
                                            jsonObj,
                                            RequestID::ID_REISTER_USER,
                                            Modules::REGISTER_MOD);
}

void RegisterController::initHttpHandlers()
{
    m_handlers.insert(RequestID::ID_GET_VARIFY_CODE, [this](QJsonObject json){
        int error = json["error"].toInt();
        if(error != ErrorCodes::SUCCESS) {
            emit verifyCodeFailed("验证码发送失败");
            qDebug() << "GET VARIFY CODE ERROR " << error;
            return;
        }

        auto email = json["email"].toString();
        auto code = json["code"].toString();
        // 发送信息给QML，告知验证码已发送到邮箱
        Q_UNUSED(code)
        emit verifyCodeSent(email);
    });

    m_handlers.insert(RequestID::ID_REISTER_USER, [this](QJsonObject json) {
        int error = json["error"].toInt();
        if(error != ErrorCodes::SUCCESS) {
            if(error == ErrorCodes::ERROR_VARIFY_EXPIRED) {
                emit registerFailed("验证码已过期");
                return;
            }
            else if(error == ErrorCodes::ERROR_VARIFYCODE) {
                emit registerFailed("验证码错误");
                return;
            }
            else if(error == ErrorCodes::ERROR_USER_EXIST) {
                emit registerFailed("用户已存在");
                return;
            }
            emit registerFailed("注册失败");
            qDebug() << "REGISTER USER ERROR " << error;
            return;
        }

        auto username = json["username"].toString();
        auto email = json["email"].toString();
        int uid = json["uid"].toInt();
        // 发送信息给QML，告知已注册成功,在注册界面显示，并且等待5s，自动跳转至登录界面
        Q_UNUSED(email)
        Q_UNUSED(uid)
        emit registerSucceeded(username);

    });
}

bool RegisterController::checkEmailValid(const QString& email)
{
    // 邮箱地址的正则表达式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch(); // 执行正则表达式匹配
    return match;
}

bool RegisterController::checkPasswordValid(const QString &password)
{
    if(password.length() < 6) return false;

    // 创建一个正则表达式对象，按照上述密码要求
    // 这个正则表达式解释：
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    bool match = regExp.match(password).hasMatch();
    return match;
}

void RegisterController::slot_register_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error)
{
    if(error != ErrorCodes::SUCCESS) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("网络异常，验证码发送失败");
        } else if (reqID == RequestID::ID_REISTER_USER) {
            emit registerFailed("网络异常，注册失败");
        }
        qDebug() << "NETWORK ERROR " << error;
        return;
    }

    // 解析json，将字节流转QJsonDocument，QJsonObject解析
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res);

    if(jsonDoc.isEmpty()) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("服务器返回数据异常");
        } else if (reqID == RequestID::ID_REISTER_USER) {
            emit registerFailed("服务器返回数据异常");
        }
        qDebug() << "JSON ANALYSIS ERROR";
        return;
    }

    if(!jsonDoc.isObject()) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("服务器返回数据异常");
        } else if (reqID == RequestID::ID_REISTER_USER) {
            emit registerFailed("服务器返回数据异常");
        }
        qDebug() << "JSON ANALYSIS ERROR";
        return;
    }

    if (!m_handlers.contains(reqID)) {
        qDebug() << "NO HANDLER FOR REQUEST ID" << reqID;
        return;
    }

    m_handlers[reqID](jsonDoc.object());
}
