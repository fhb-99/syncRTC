#ifndef HTTPMGR_H
#define HTTPMGR_H

#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>

#include "../models/Singleton.h"
#include "../models/global.h"

class HttpMgr : public QObject, public Singleton<HttpMgr>,
                public std::enable_shared_from_this<HttpMgr>
{
    Q_OBJECT
    friend class Singleton<HttpMgr>;
public:
    ~HttpMgr() = default;

    void PostHttpRequest(QUrl url, QJsonObject jsonObj, RequestID reqID, Modules module);
private:
    explicit HttpMgr(QObject *parent = nullptr);

    QNetworkAccessManager m_manager;

signals:
    void signal_http_finish(RequestID reqID, QByteArray res, ErrorCodes error, Modules module);

    void signal_login_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);
    void signal_register_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);
    void signal_reset_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);
    void signal_contacts_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);

public slots:
    void slot_http_finish(RequestID reqID, QByteArray res, ErrorCodes error, Modules module);

};

#endif // HTTPMGR_H
