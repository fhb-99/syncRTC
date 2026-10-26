#include "tcpmgr.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QtEndian>
#include <limits>
#include <algorithm>

namespace {
constexpr qint64 kHeartbeatIntervalMs = 5000;
constexpr qint64 kHeartbeatTimeoutMs = 15000;
}

TcpMgr::TcpMgr(QObject *parent) : QObject(parent)
{
    m_connectTimer.setSingleShot(true);
    m_connectTimer.setInterval(5000);
    connect(&m_connectTimer, &QTimer::timeout, this, &TcpMgr::connectionLost);
    m_heartbeatTimer.setInterval(1000);
    m_heartbeatTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &TcpMgr::heartbeatTick);
    connect(this, &TcpMgr::signal_send_data, this, &TcpMgr::slot_send_data);
}

void TcpMgr::closeConnection()
{
    // 先使回调失效，再 abort；旧半包和待写字节不能进入下一条连接。
    ++m_generation;
    m_connectTimer.stop();
    m_heartbeatTimer.stop();
    m_probes.clear();
    m_heartbeatRtt = -1;
    m_businessReady = false;
    m_buffer.clear();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void TcpMgr::connectionLost()
{
    if (!m_socket) return;
    closeConnection();
    emit signal_connect_success(false);
    emit signal_connection_lost();
}

void TcpMgr::slot_tcp_connect(ServerInfo server)
{
    closeConnection();
    auto *socket = new QTcpSocket(this);
    m_socket = socket;
    const quint64 generation = m_generation;
    socket->setProxy(QNetworkProxy::NoProxy);
    connect(socket, &QTcpSocket::connected, this, [this, generation] {
        if (generation != m_generation) return;
        m_connectTimer.stop();
        emit signal_connect_success(true);
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [this, generation](QAbstractSocket::SocketError) {
        if (generation == m_generation) connectionLost();
    });
    connect(socket, &QTcpSocket::disconnected, this, [this, generation] {
        if (generation == m_generation) connectionLost();
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, generation] {
        if (generation == m_generation) readIncoming();
    });
    m_connectTimer.start();
    socket->connectToHost(server.host, server.port.toUShort());
}

void TcpMgr::readIncoming()
{
    if (!m_socket) return;
    const auto generation = m_generation;
    m_buffer.append(m_socket->readAll());
    while (generation == m_generation && m_buffer.size() >= 4) {
        const char *header = m_buffer.constData();
        const auto id = qFromBigEndian<quint16>(header);
        const auto size = qFromBigEndian<quint16>(header + 2);
        if (m_buffer.size() < 4 + size) return;
        const QByteArray payload = m_buffer.mid(4, size);
        m_buffer.remove(0, 4 + size);
        if (id == ID_HEARTBEAT_RESPONSE && size > 96) continue;
        const QJsonDocument document = QJsonDocument::fromJson(payload);
        if (!document.isObject()) {
            connectionLost();
            return;
        }
        // 心跳在 socket 所属线程结束；业务回调可能断开，下一轮重新检查代次。
        if (id == ID_HEARTBEAT_RESPONSE) acceptHeartbeat(document.object());
        else emit signal_message_recv(static_cast<RequestID>(id), document.object());
    }
}

void TcpMgr::startHeartbeat(bool sequenced)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_sequencedHeartbeat = sequenced;
    m_probes.clear();
    m_pauseReprobeUsed = false;
    m_reprobeUntil = 0;
    m_heartbeatRtt = -1;
    m_lastHeartbeatAck = m_lastHeartbeatTick = 0;
    m_heartbeatClock.start();
    m_heartbeatTimer.start();
    sendHeartbeat(0);
}

void TcpMgr::sendHeartbeat(qint64 now)
{
    // 队列提交也计入探测期限，不能让尚未写入内核的探测无限等待。
    while (!m_probes.empty() && now - m_probes.front().sentAt >= kHeartbeatTimeoutMs)
        m_probes.pop_front();
    if (m_probes.size() >= 4) m_probes.pop_front();
    const QString sequence = QString::number(++m_heartbeatSequence);
    m_probes.push_back({sequence, now});
    m_lastHeartbeatSent = now;
    const QJsonObject request = m_sequencedHeartbeat ? QJsonObject{{"seq", sequence}} : QJsonObject{};
    slot_send_data(ID_HEARTBEAT_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void TcpMgr::acceptHeartbeat(const QJsonObject &response)
{
    if (!m_heartbeatTimer.isActive() || m_probes.empty()) return;
    const auto now = m_heartbeatClock.elapsed();
    auto probe = m_probes.begin();
    if (m_sequencedHeartbeat) {
        if (response.size() != 1 || !response.value("seq").isString()) return;
        const auto sequence = response.value("seq").toString();
        probe = std::find_if(m_probes.begin(), m_probes.end(), [&sequence](const Probe &item) {
            return item.sequence == sequence;
        });
    } else if (!response.isEmpty()) return;
    if (probe == m_probes.end() || now - probe->sentAt >= kHeartbeatTimeoutMs) return;
    m_heartbeatRtt = now - probe->sentAt;
    m_lastHeartbeatAck = now;
    m_pauseReprobeUsed = false;
    m_reprobeUntil = 0;
    // 确认新探测后，旧响应和重复响应都不再具有续命资格。
    m_probes.erase(m_probes.begin(), std::next(probe));
}

void TcpMgr::heartbeatTick()
{
    if (!m_heartbeatTimer.isActive()) return;
    const auto generation = m_generation;
    const auto now = m_heartbeatClock.elapsed();
    const bool delayed = now - m_lastHeartbeatTick > kHeartbeatIntervalMs;
    m_lastHeartbeatTick = now;
    // 系统恢复调度时先消化已到达的数据，再判断期限，避免定时器抢先误判。
    readIncoming();
    if (generation != m_generation || !m_heartbeatTimer.isActive()) return;
    if (now - m_lastHeartbeatAck >= kHeartbeatTimeoutMs) {
        if (delayed && !m_pauseReprobeUsed) {
            m_pauseReprobeUsed = true;
            m_reprobeUntil = now + kHeartbeatIntervalMs;
            m_probes.clear();
            sendHeartbeat(now);
            return;
        }
        if (now >= m_reprobeUntil) {
            qWarning() << "[TcpMgr] heartbeat timeout, pending bytes=" << m_socket->bytesToWrite();
            connectionLost(); // 与 socket 错误、EOF 共用唯一失效入口。
            return;
        }
    }
    if (now - m_lastHeartbeatSent >= kHeartbeatIntervalMs) sendHeartbeat(now);
}

void TcpMgr::slot_send_data(RequestID reqID, QByteArray data)
{
    const bool recoveryRequest = reqID == AUTH_LOGIN_REQUEST ||
        reqID == ID_RESUME_MEETING_REQUEST || reqID == ID_HEARTBEAT_REQUEST;
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState ||
        (!m_businessReady && !recoveryRequest) ||
        data.size() > std::numeric_limits<quint16>::max()) {
        emit signal_send_rejected(reqID, data);
        return;
    }
    if (m_socket->bytesToWrite() + data.size() + 4 > 4 * 1024 * 1024) {
        connectionLost();
        emit signal_send_rejected(reqID, data);
        return;
    }
    QByteArray frame;
    QDataStream stream(&frame, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << static_cast<quint16>(reqID) << static_cast<quint16>(data.size());
    frame.append(data);
    if (m_socket->write(frame) != frame.size()) connectionLost();
}
