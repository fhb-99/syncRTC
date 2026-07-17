#include "PasswordResetController.h"

#include "../../network/httpmgr.h"

#include <QDebug>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QJsonDocument>
#include <QJsonObject>

PasswordResetController::PasswordResetController(QObject *parent)
    : QObject(parent)
{
    // 注册对应的http处理，信号来了，可以直接执行相关函数
    initHttpHandlers();
}

void PasswordResetController::GetVarifyCodeAsync(const QString &email)
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
                                            Modules::RESET_MOD);
}

void PasswordResetController::ReSetPassword(const QString& email,const QString& code,
                                            const QString& password, const QString& confirm)
{
    if(!checkEmailValid(email)) {
        emit resetPasswordFailed("邮箱地址不正确");
        qDebug() << "邮箱地址不正确";
        return;
    }

    if(!checkPasswordValid(password))
    {
        emit resetPasswordFailed("密码格式不正确");
        qDebug() << "密码格式不正确";
        return;
    }

    QJsonObject jsonObj;
    jsonObj["email"] = email;
    jsonObj["confirm"] = confirm;
    jsonObj["password"] = password;
    jsonObj["VarifyCode"] = code;
    HttpMgr::GetInstance()->PostHttpRequest(QUrl(GateServer_URL + "/reset_pwd"),
                                            jsonObj,
                                            RequestID::ID_RESET_USER,
                                            Modules::RESET_MOD);
}

void PasswordResetController::initHttpHandlers()
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
        // 发送信息给QML，告知验证码已发送到邮箱 todo
        Q_UNUSED(code)
        emit verifyCodeSent(email);

    });

    m_handlers.insert(RequestID::ID_RESET_USER, [this](QJsonObject json){
        int error = json["error"].toInt();
        if(error != ErrorCodes::SUCCESS) {
            emit resetPasswordFailed("重置密码失败");
            qDebug() << "RESET PASSWORD ERROR " << error;
            return;
        }

        auto email = json["email"].toString();
        // 发送信息给QML，重置密码成功，给个提示即可，不用自动跳转 todo
        Q_UNUSED(email)
        emit resetPasswordSucceeded();

    });
}

bool PasswordResetController::checkEmailValid(const QString &email)
{
    // 邮箱地址的正则表达式
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch(); // 执行正则表达式匹配
    return match;
}

bool PasswordResetController::checkPasswordValid(const QString &password)
{
    if(password.length() < 6) return false;

    // 创建一个正则表达式对象，按照上述密码要求
    // 这个正则表达式解释：
    // ^[a-zA-Z0-9!@#$%^&*]{6,15}$ 密码长度至少6，可以是字母、数字和特定的特殊字符
    QRegularExpression regExp("^[a-zA-Z0-9!@#$%^&*.]{6,15}$");
    bool match = regExp.match(password).hasMatch();
    return match;
}

void PasswordResetController::slot_reset_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error)
{
    if(error != ErrorCodes::SUCCESS) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("网络异常，验证码发送失败");
        } else if (reqID == RequestID::ID_RESET_USER) {
            emit resetPasswordFailed("网络异常，重置密码失败");
        }
        qDebug() << "NETWORK ERROR " << error;
        return;
    }

    // 解析json，将字节流转QJsonDocument，QJsonObject解析
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res);

    if(jsonDoc.isEmpty()) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("服务器返回数据异常");
        } else if (reqID == RequestID::ID_RESET_USER) {
            emit resetPasswordFailed("服务器返回数据异常");
        }
        qDebug() << "JSON ANALYSIS ERROR";
        return;
    }

    if(!jsonDoc.isObject()) {
        if (reqID == RequestID::ID_GET_VARIFY_CODE) {
            emit verifyCodeFailed("服务器返回数据异常");
        } else if (reqID == RequestID::ID_RESET_USER) {
            emit resetPasswordFailed("服务器返回数据异常");
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
