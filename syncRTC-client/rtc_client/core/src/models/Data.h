#ifndef DATA_H
#define DATA_H

#include <QString>
#include <QJsonObject>

struct ServerInfo {
    int uid;
    QString host;
    QString port;
    QString token;
};

struct UserData {
    int uid;
    QString username;
    QString email;
    QString password;

    // 结构体转QJsonObject
    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["username"] = username;
        obj["email"] = email;
        obj["password"] = password;
        return obj;
    }

    // 静态反序列化：JSON转回UserData
    static UserData fromJson(const QJsonObject& obj)
    {
        UserData data;
        data.username = obj["username"].toString();
        data.email = obj["email"].toString();
        data.password = obj["password"].toString();
        return data;
    }
};

#endif // DATA_H
