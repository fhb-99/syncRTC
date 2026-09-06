#ifndef PASSWORDRESETCONTROLLER_H
#define PASSWORDRESETCONTROLLER_H

#include <QObject>
#include <QMap>

#include "../../models/Data.h"
#include "../../models/global.h"

class PasswordResetController : public QObject
{
    Q_OBJECT

public:
    explicit PasswordResetController(QObject *parent = nullptr);

    // Q_INVOKABLE 让 QML 可以直接调用这个函数
    //函数内部是异步调用
    Q_INVOKABLE void GetVarifyCodeAsync(const QString& email);
    Q_INVOKABLE void ReSetPassword(const QString& email,const QString& code,
                                   const QString& password, const QString& confirm);

private:
    QMap<RequestID, std::function<void(const QJsonObject&)>> m_handlers;

    void initHttpHandlers();

    // 对界面传来的数据进行二次检验
    bool checkEmailValid(const QString& email);
    bool checkPasswordValid(const QString& password);

public slots:
    void slot_reset_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);

signals:
    void verifyCodeSent(const QString& email);
    void verifyCodeFailed(const QString& reason);

    void resetPasswordSucceeded();
    void resetPasswordFailed(const QString& reason);

};

#endif // PASSWORDRESETCONTROLLER_H
