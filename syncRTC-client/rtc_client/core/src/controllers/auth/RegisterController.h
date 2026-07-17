#ifndef REGISTERCONTROLLER_H
#define REGISTERCONTROLLER_H

#include <QObject>
#include <QMap>

#include "../../models/Data.h"
#include "../../models/global.h"

class RegisterController : public QObject
{
    Q_OBJECT

public:
    explicit RegisterController(QObject *parent = nullptr);

    // Q_INVOKABLE 让 QML 可以直接调用这个函数
    //函数内部是异步调用
    Q_INVOKABLE void GetVarifyCodeAsync(const QString& email);
    Q_INVOKABLE void RegisterRequest(const QString& username, const QString& email,
                                    const QString& varifycode, const QString& password,
                                     const QString& comfirm);

private:
    QMap<RequestID, std::function<void(const QJsonObject&)>> m_handlers;

    void initHttpHandlers();

    // 对界面传来的数据进行二次检验
    bool checkEmailValid(const QString& email);
    bool checkPasswordValid(const QString& password);

public slots:
    void slot_register_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);

signals:
    // 异步校验结果通过信号返回，QML 用 Connections 接收
    void verifyCodeSent(const QString& email);
    void verifyCodeFailed(const QString& reason);

    void registerSucceeded(const QString& username);
    void registerFailed(const QString& reason);
};

#endif // REGISTERCONTROLLER_H
