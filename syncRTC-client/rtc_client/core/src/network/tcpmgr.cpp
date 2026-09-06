#include "tcpmgr.h"

#include <QNetworkProxy>
#include <QDataStream>
#include <QIODevice>
#include <QtEndian>
#include <qjsondocument.h>
#include <QJsonObject>

TcpMgr::TcpMgr(QObject *parent)
    : QObject{parent},
    m_host(""),
    m_port(0),
    is_recv_pending(false),
    m_message_id(0),
    m_message_len(0)
{
    m_socket.setProxy(QNetworkProxy::NoProxy);

    // 处理错误
    QObject::connect(&m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), [&](QAbstractSocket::SocketError socketError) {
        Q_UNUSED(socketError)
        qDebug() << "Error:" << m_socket.errorString();
    });

    // 连接建立
    QObject::connect(&m_socket, &QTcpSocket::connected, [&](){
        qDebug() << "CONNECTED TO SERVER";
        emit signal_connect_success(true);
    });

    // 读数据
    QObject::connect(&m_socket, &QTcpSocket::readyRead, [&](){
        // 当有数据可读时，读取所有数据
        // 读取所有数据并追加到缓冲区
        m_buffer.append(m_socket.readAll());
        const int HEAD_LEN = sizeof(quint16) * 2;

        for( ;; ) {
            // 先解析头部
            if(!is_recv_pending) {
                // 检查缓冲区内的数据是否可以解析出一个消息头
                if(m_buffer.size() < HEAD_LEN) {
                    break;
                }

                // 直接从缓冲区读取头部，避免 QDataStream 内部读位置在 _buffer
                // 被 mid() / remove() 裁剪后残留，导致后续消息头部解析偏移错误
                const char * ptr = m_buffer.constData();
                m_message_id = qFromBigEndian<quint16>(ptr);
                m_message_len = qFromBigEndian<quint16>(ptr + sizeof(quint16));

                // 将buffer中的前四个字节移除，开始读真正的数据
                m_buffer.remove(0, HEAD_LEN);
            }
            //buffer剩余长读是否满足消息体长度，不满足则退出继续等待接受
            if(m_buffer.size() < m_message_len) {
                is_recv_pending = true;
                break;
            }
            is_recv_pending = false;

            QByteArray data = m_buffer.left(m_message_len);
            m_buffer.remove(0, m_message_len);
            handleMsg(RequestID(m_message_id), m_message_len, data);
        }
    });

    // 连接断开
    QObject::connect(&m_socket, &QTcpSocket::disconnected, [&](){
        qDebug() << "DISCONNECTED FROM SERVER";
    });

    // 发送数据
    QObject::connect(this, &TcpMgr::signal_send_data, this, &TcpMgr::slot_send_data);

    initHandlers();
}

void TcpMgr::initHandlers()
{
    // TcpMgr 只负责解析 JSON 并上抛，不解释具体业务语义。
    const auto forwardJsonResponse = [this](RequestID id, int len, QByteArray data) {
        Q_UNUSED(len)

        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);
        if (!jsonDoc.isObject()) {
            qDebug() << "FAILED TO CREATE RESPONSE JSON";
            return;
        }

        // TcpMgr 只解帧和解析 JSON，不解释登录成功或失败等业务含义
        emit signal_message_recv(id, jsonDoc.object());
    };

    m_handlers.insert(AUTH_LOGIN_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_CREATE_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_PAST_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_JOIN_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_START_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_STARTED, forwardJsonResponse);
    m_handlers.insert(ID_LEAVE_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_END_MEETING_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_ENDED, forwardJsonResponse);
    m_handlers.insert(ID_SEND_MEETING_MESSAGE_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MESSAGE_PUSH, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MEMBER_JOINED, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MEMBER_LEFT, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MEMBER_RECONNECTING, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MEMBER_RECONNECTED, forwardJsonResponse);
    m_handlers.insert(ID_MEETING_MEMBER_TIMEOUT_LEFT, forwardJsonResponse);
    m_handlers.insert(ID_GET_MEETING_GROUP_MESSAGES_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_GET_MEETING_PRIVATE_MESSAGES_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEDIA_ANSWER_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEDIA_CANDIDATE_RESPONSE, forwardJsonResponse);
    m_handlers.insert(ID_MEDIA_RENEGOTIATION_OFFER, forwardJsonResponse);
}

void TcpMgr::handleMsg(RequestID id, int len, QByteArray data)
{
    auto find_itor = m_handlers.find(id);
    if(find_itor == m_handlers.end()) {
        qDebug() << "NOT FIND ID [" << id << "]";
        return;
    }

    find_itor.value()(id, len, data);
}

void TcpMgr::slot_tcp_connect(ServerInfo server)
{
    // 外部其他发送信号到该槽函数，来建立tcp连接
    qDebug() << "CONNECTING TO SERVER......";
    m_host = server.host;
    m_port = static_cast<uint16_t>(server.port.toUInt());
    m_socket.connectToHost(m_host, m_port);
}

void TcpMgr::slot_send_data(RequestID reqID, QByteArray data)
{
    uint16_t id = reqID;

    // 计算长度
    quint16 len = static_cast<quint16>(data.length());

    // 创建一个QByteArray来存储要发送的数据
    QByteArray target;
    QDataStream stream(&target, QIODevice::WriteOnly);

    // 设置网络字节序，用大端还是小端
    stream.setByteOrder(QDataStream::BigEndian);

    // 写入id和len
    stream << id << len;

    // 写入数据
    target.append(data);

    // 发送数据，write是异步发送
    m_socket.write(target);
    qDebug() << "TcpMgr is Sending data";
}
