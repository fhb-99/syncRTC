#include "httpmgr.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

void HttpMgr::PostHttpRequest(QUrl url, QJsonObject jsonObj, RequestID reqID, Modules module)
{
    QJsonDocument jsonDoc = QJsonDocument(jsonObj);
    const QByteArray data = jsonDoc.toJson(QJsonDocument::Compact);

    qDebug() << "[HttpMgr] Post——url is " << url.toString() <<
        ", RequestID is " << static_cast<int>(reqID) <<
        ", Module is " << static_cast<int>(module);

    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.size()));

    // post函数本质上也是异步
    QNetworkReply * reply = m_manager.post(request, data);

    // 设置超时时长，异步触发
    QTimer::singleShot(5000, reply, [reply, url](){
        if(reply->isFinished()) {
            return;
        }
        qDebug() << "[HttpMgr] timeout, aborting: " << url.toString();
        reply->abort();
    });

    auto self = shared_from_this();
    QObject::connect(reply, &QNetworkReply::finished, [self, reply, reqID, module](){
        const int http_status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if(reply->error() != QNetworkReply::NoError) {
            qDebug() << "[HttpMgr] HTTP error, status=" << http_status
                     << ", qt_err=" << reply->error()
                     << reply->errorString();

            emit self->signal_http_finish(reqID, "", ErrorCodes::ERROR_NETWORK, module);
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        qDebug() << "[HttpMgr] HTTP OK, status = " << http_status << "len = " << data.size();

        emit self->signal_http_finish(reqID, data, ErrorCodes::SUCCESS, module);
        reply->deleteLater();
    });
}

HttpMgr::HttpMgr(QObject *parent)
    : QObject{parent}
{
    connect(this, &HttpMgr::signal_http_finish, this, &HttpMgr::slot_http_finish);
}

void HttpMgr::slot_http_finish(RequestID reqID, QByteArray res, ErrorCodes error, Modules module)
{
    if(module == Modules::LOGIN_MOD) {
        emit signal_login_mod_finish(reqID, res, error);
    }
    else if(module == Modules::REGISTER_MOD) {
        emit signal_register_mod_finish(reqID, res, error);
    }
    else if(module == Modules::RESET_MOD) {
        emit signal_reset_mod_finish(reqID, res, error);
    }
}
