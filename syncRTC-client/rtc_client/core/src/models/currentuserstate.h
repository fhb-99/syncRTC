#ifndef CURRENTUSERSTATE_H
#define CURRENTUSERSTATE_H

#include <QObject>

class CurrentUserState : public QObject
{
    Q_OBJECT
    // QML 通过这两个属性读取当前用户资料；NOTIFY 让界面在资料更新后自动刷新。
    Q_PROPERTY(QString username READ username NOTIFY usernameChanged)
    Q_PROPERTY(QString email READ email NOTIFY emailChanged)
public:
    explicit CurrentUserState(QObject *parent = nullptr);

    QString username() const;
    QString email() const;

    void setEmail(const QString &email);
    void setUsername(const QString &username);

private:
    QString m_email;
    QString m_username;

signals:
    void usernameChanged();
    void emailChanged();

};

#endif // CURRENTUSERSTATE_H
