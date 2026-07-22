#ifndef TCPMGR_H
#define TCPMGR_H

#include <QObject>
#include <memory>
#include <QTcpSocket>
#include <QMap>
#include <QJsonArray>
#include <functional>

#include "../models/global.h"
#include "../models/Singleton.h"
#include "../models/Data.h"

class TcpMgr : public QObject, public Singleton<TcpMgr>,
               public std::enable_shared_from_this<TcpMgr>
{
    Q_OBJECT
    friend class Singleton<TcpMgr>;
public:
    ~TcpMgr() = default;
private:
    explicit TcpMgr(QObject *parent = nullptr);

    void initHandlers();
    void handleMsg(RequestID id, int len, QByteArray data);
    QMap<RequestID, std::function<void(RequestID reqID, int len, QByteArray data)>> m_handlers;

    QTcpSocket m_socket;
    QString m_host;
    uint16_t m_port;
    QByteArray m_buffer;
    bool is_recv_pending;
    quint16 m_message_id;
    quint16 m_message_len;

public slots:
    void slot_tcp_connect(ServerInfo server);
    void slot_send_data(RequestID reqID, QByteArray data);
signals:
    void signal_connect_success(bool success);
    void signal_send_data(RequestID reqID, QByteArray data);
    void signal_login_failed(int);

    // TcpMgr 只转发解析后的回包，业务分发由 RealtimeController 完成。
    void signal_message_recv(RequestID, QJsonObject);
};

#endif // TCPMGR_H
