#ifndef TCPMGR_H
#define TCPMGR_H

#include <QObject>
#include <memory>
#include <QTcpSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <deque>
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
    friend struct TcpHeartbeatTestAccess;
public:
    ~TcpMgr() = default;
    void closeConnection();
    // 鉴权成功后启用；序号模式由服务端能力协商，生命周期跟随当前 socket。
    void startHeartbeat(bool sequenced);
    qint64 heartbeatRttMs() const { return m_heartbeatRtt; }
    void setBusinessReady(bool ready) { m_businessReady = ready; }
    bool businessReady() const { return m_businessReady; }
private:
    explicit TcpMgr(QObject *parent = nullptr);
    void connectionLost();
    void readIncoming();
    void heartbeatTick();
    void sendHeartbeat(qint64 now);
    void acceptHeartbeat(const QJsonObject &response);
    QTcpSocket *m_socket = nullptr;
    QTimer m_connectTimer;
    quint64 m_generation = 0;
    bool m_businessReady = false;
    QByteArray m_buffer;
    struct Probe { QString sequence; qint64 sentAt; };
    QTimer m_heartbeatTimer;
    QElapsedTimer m_heartbeatClock;
    std::deque<Probe> m_probes;
    quint64 m_heartbeatSequence = 0;
    bool m_sequencedHeartbeat = false;
    bool m_pauseReprobeUsed = false;
    qint64 m_lastHeartbeatAck = 0;
    qint64 m_lastHeartbeatSent = 0;
    qint64 m_lastHeartbeatTick = 0;
    qint64 m_reprobeUntil = 0;
    qint64 m_heartbeatRtt = -1;

public slots:
    void slot_tcp_connect(ServerInfo server);
    void slot_send_data(RequestID reqID, QByteArray data);
signals:
    void signal_connect_success(bool success);
    void signal_send_data(RequestID reqID, QByteArray data);
    void signal_login_failed(int);
    void signal_connection_lost();
    void signal_send_rejected(RequestID, QByteArray);

    // TcpMgr 只转发解析后的回包，业务分发由 RealtimeController 完成。
    void signal_message_recv(RequestID, QJsonObject);
};

#endif // TCPMGR_H
