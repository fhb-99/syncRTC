#include "chatcontroller.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QUuid>

#include "../../models/global.h"
#include "../../network/tcpmgr.h"

ChatController::ChatController(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ChatController::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_messages.size();
}

QVariant ChatController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_messages.size()) {
        return {};
    }

    const ChatMessage &message = m_messages.at(index.row());
    switch (role) {
    case MessageIdRole: return message.messageId;
    case ClientMsgIdRole: return message.clientMsgId;
    case MeetingIdRole: return message.meetingId;
    case ChatTypeRole: return message.chatType;
    case SenderUserIdRole: return message.senderUserId;
    case SenderNameRole: return message.senderName;
    case ReceiverUserIdRole: return message.receiverUserId;
    case ContentRole: return message.content;
    case CreatedAtRole: return message.createdAt;
    case DeliveryStateRole: return message.deliveryState;
    case IsMineRole: return message.isMine;
    default: return {};
    }
}

QHash<int, QByteArray> ChatController::roleNames() const
{
    return {
        {MessageIdRole, "messageId"},
        {ClientMsgIdRole, "clientMsgId"},
        {MeetingIdRole, "meetingId"},
        {ChatTypeRole, "chatType"},
        {SenderUserIdRole, "senderUserId"},
        {SenderNameRole, "senderName"},
        {ReceiverUserIdRole, "receiverUserId"},
        {ContentRole, "content"},
        {CreatedAtRole, "createdAt"},
        {DeliveryStateRole, "deliveryState"},
        {IsMineRole, "isMine"},
    };
}

void ChatController::setActiveMeetingId(const QString &meetingId)
{
    if (m_activeMeetingId == meetingId) {
        return;
    }

    beginResetModel();
    m_activeMeetingId = meetingId;
    m_messages.clear();
    endResetModel();
    emit messagesChanged();
}

void ChatController::sendGroupMessage(const QString &meetingId, const QString &content)
{
    sendMessage(meetingId, QStringLiteral("group"), QString(), content);
}

void ChatController::sendPrivateMessage(const QString &meetingId, qint64 targetUserId,
                                        const QString &content)
{
    if (targetUserId <= 0) {
        return;
    }

    sendMessage(meetingId, QStringLiteral("private"), QString::number(targetUserId), content);
}

void ChatController::sendMessage(const QString &meetingId, const QString &chatType,
                                 const QString &targetUserId, const QString &content)
{
    const QString text = content.trimmed();
    if (meetingId.isEmpty() || meetingId != m_activeMeetingId || text.isEmpty() ||
        (chatType == QStringLiteral("private") && targetUserId.isEmpty())) {
        return;
    }

    ChatMessage message;
    message.clientMsgId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    message.meetingId = meetingId;
    message.chatType = chatType;
    message.senderName = QStringLiteral("我");
    message.receiverUserId = targetUserId;
    message.content = text;
    message.createdAt = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    message.deliveryState = QStringLiteral("sending");
    message.isMine = true;

    const int row = m_messages.size();
    beginInsertRows(QModelIndex(), row, row);
    m_messages.append(message);
    endInsertRows();
    emit messagesChanged();

    QJsonObject request;
    request[QStringLiteral("meeting_id")] = meetingId;
    request[QStringLiteral("chat_type")] = chatType;
    request[QStringLiteral("target_user_id")] = chatType == QStringLiteral("group")
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(targetUserId.toLongLong());
    request[QStringLiteral("content")] = text;
    request[QStringLiteral("client_msg_id")] = message.clientMsgId;
    emit TcpMgr::GetInstance()->signal_send_data(
        ID_SEND_MEETING_MESSAGE_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

bool ChatController::applySendMessageAck(const QJsonObject &json)
{
    const QString clientMsgId = json.value(QStringLiteral("client_msg_id")).toString();
    if (clientMsgId.isEmpty()) {
        return false;
    }

    const int row = findMessage(clientMsgId);
    if (row < 0) {
        return true;
    }

    const int error = json.value(QStringLiteral("error")).toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        m_messages[row].deliveryState = QStringLiteral("failed");
        const QModelIndex modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {DeliveryStateRole});
        emit messagesChanged();
        emit messageSendFailed(clientMsgId, error);
        return true;
    }

    ChatMessage message;
    if (!messageFromJson(json, message) || message.meetingId != m_activeMeetingId) {
        return false;
    }

    message.deliveryState = QStringLiteral("sent");
    message.isMine = true;
    updateMessage(row, message);
    return true;
}

bool ChatController::applyMessageReceived(const QJsonObject &json)
{
    ChatMessage message;
    if (!messageFromJson(json, message)) {
        return false;
    }
    if (message.meetingId != m_activeMeetingId) {
        return true;
    }

    const int row = findMessage(message.clientMsgId);
    if (row >= 0) {
        message.deliveryState = QStringLiteral("sent");
        message.isMine = true;
        updateMessage(row, message);
        return true;
    }

    message.deliveryState = QStringLiteral("sent");
    message.isMine = false;
    const int newRow = m_messages.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_messages.append(message);
    endInsertRows();
    emit messagesChanged();
    return true;
}

bool ChatController::messageFromJson(const QJsonObject &json, ChatMessage &message)
{
    message.messageId = json.value(QStringLiteral("message_id")).toString();
    message.clientMsgId = json.value(QStringLiteral("client_msg_id")).toString();
    message.meetingId = json.value(QStringLiteral("meeting_id")).toString();
    message.chatType = json.value(QStringLiteral("chat_type")).toString();
    message.senderUserId = json.value(QStringLiteral("sender_user_id")).toVariant().toLongLong();
    message.senderName = json.value(QStringLiteral("sender_name")).toString();
    message.receiverUserId = json.value(QStringLiteral("receiver_user_id")).toVariant().toString();
    message.content = json.value(QStringLiteral("content")).toString();
    message.createdAt = json.value(QStringLiteral("created_at")).toString();

    const bool isGroup = message.chatType == QStringLiteral("group");
    const bool isPrivate = message.chatType == QStringLiteral("private") &&
                           message.receiverUserId.toLongLong() > 0;
    return !message.messageId.isEmpty() && !message.clientMsgId.isEmpty() &&
           !message.meetingId.isEmpty() && (isGroup || isPrivate) &&
           message.senderUserId > 0 && !message.senderName.isEmpty() &&
           !message.content.isEmpty() && !message.createdAt.isEmpty();
}

int ChatController::findMessage(const QString &clientMsgId) const
{
    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).clientMsgId == clientMsgId) {
            return row;
        }
    }
    return -1;
}

void ChatController::updateMessage(int row, const ChatMessage &message)
{
    m_messages[row] = message;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex);
    emit messagesChanged();
}
