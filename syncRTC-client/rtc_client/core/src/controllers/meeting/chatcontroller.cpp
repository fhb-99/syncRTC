#include "chatcontroller.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTime>
#include <QUuid>

#include "../../models/global.h"
#include "../../network/tcpmgr.h"

namespace {

QString formatMessageTimeForDisplay(const QString &createdAt)
{
    const QString value = createdAt.trimmed();
    if (value.isEmpty()) {
        return {};
    }

    const QTime timeOnly = QTime::fromString(value, QStringLiteral("HH:mm"));
    if (timeOnly.isValid()) {
        return value;
    }

    QString normalized = value;
    const int dotIndex = normalized.indexOf(QLatin1Char('.'));
    if (dotIndex >= 0 && normalized.size() > dotIndex + 4) {
        normalized = normalized.left(dotIndex + 4);
    }

    QDateTime serverTime = QDateTime::fromString(
        normalized, QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    if (!serverTime.isValid()) {
        serverTime = QDateTime::fromString(
            normalized, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
    if (!serverTime.isValid()) {
        return value;
    }

    // MySQL 容器默认按 UTC 生成消息时间，展示前转成本机时区，避免界面显示慢 8 小时。
    serverTime.setTimeSpec(Qt::UTC);
    return serverTime.toLocalTime().toString(QStringLiteral("HH:mm"));
}

} // namespace

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

void ChatController::requestGroupHistory(const QString &meetingId,
                                         const QString &beforeMessageId,
                                         int limit)
{
    requestHistory(meetingId, QStringLiteral("group"), QString(), beforeMessageId, limit);
}

void ChatController::requestHistory(const QString &meetingId,
                                    const QString &chatType,
                                    const QString &peerUserId,
                                    const QString &beforeMessageId,
                                    int limit)
{
    if (meetingId.isEmpty() || meetingId != m_activeMeetingId) {
        return;
    }
    if (chatType != QStringLiteral("group") &&
        (chatType != QStringLiteral("private") || peerUserId.toLongLong() <= 0)) {
        return;
    }

    // 历史消息由打开会话面板或滚动到顶部自动触发；
    // before_message_id 为空表示取最新一批，不为空表示取这条消息之前的更早记录。
    QJsonObject request;
    request[QStringLiteral("meeting_id")] = meetingId;
    request[QStringLiteral("before_message_id")] =
        static_cast<qint64>(beforeMessageId.toULongLong());
    request[QStringLiteral("limit")] = qBound(1, limit, 100);
    if (chatType == QStringLiteral("private")) {
        request[QStringLiteral("peer_user_id")] = peerUserId.toLongLong();
    }

    const RequestID requestId = chatType == QStringLiteral("group")
        ? ID_GET_MEETING_GROUP_MESSAGES_REQUEST
        : ID_GET_MEETING_PRIVATE_MESSAGES_REQUEST;
    emit TcpMgr::GetInstance()->signal_send_data(
        requestId, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

QString ChatController::earliestMessageId(const QString &chatType,
                                          const QString &peerUserId) const
{
    for (const ChatMessage &message : m_messages) {
        if (belongsToConversation(message, chatType, peerUserId)) {
            return message.messageId;
        }
    }
    return {};
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

bool ChatController::applyGroupHistoryResponse(const QJsonObject &json)
{
    const QString meetingId = json.value(QStringLiteral("meeting_id")).toString();
    if (meetingId != m_activeMeetingId) {
        return true;
    }

    const int error = json.value(QStringLiteral("error")).toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        emit groupHistoryLoadFailed(error);
        return true;
    }

    const QJsonValue messagesValue = json.value(QStringLiteral("messages"));
    if (!messagesValue.isArray()) {
        return false;
    }

    QVector<ChatMessage> historyMessages;
    const QJsonArray messages = messagesValue.toArray();
    historyMessages.reserve(messages.size());
    for (const QJsonValue &messageValue : messages) {
        if (!messageValue.isObject()) {
            return false;
        }

        ChatMessage message;
        if (!messageFromJson(messageValue.toObject(), message) ||
            message.chatType != QStringLiteral("group")) {
            return false;
        }

        message.deliveryState = QStringLiteral("sent");
        historyMessages.append(message);
    }

    const int addedCount = mergeHistoryMessages(historyMessages);
    const int pageLimit = json.value(QStringLiteral("limit")).toInt(50);
    emit historyMessagesLoaded(QStringLiteral("group"),
                               QString(),
                               addedCount,
                               messages.size() >= pageLimit);
    return true;
}

bool ChatController::applyPrivateHistoryResponse(const QJsonObject &json)
{
    const QString meetingId = json.value(QStringLiteral("meeting_id")).toString();
    const QString peerUserId =
        QString::number(json.value(QStringLiteral("peer_user_id")).toVariant().toLongLong());
    if (meetingId != m_activeMeetingId) {
        return true;
    }

    const int error = json.value(QStringLiteral("error")).toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        emit privateHistoryLoadFailed(peerUserId, error);
        return true;
    }

    const QJsonValue messagesValue = json.value(QStringLiteral("messages"));
    if (!messagesValue.isArray() || peerUserId.toLongLong() <= 0) {
        return false;
    }

    QVector<ChatMessage> historyMessages;
    const QJsonArray messages = messagesValue.toArray();
    historyMessages.reserve(messages.size());
    for (const QJsonValue &messageValue : messages) {
        if (!messageValue.isObject()) {
            return false;
        }

        ChatMessage message;
        if (!messageFromJson(messageValue.toObject(), message) ||
            !belongsToConversation(message, QStringLiteral("private"), peerUserId)) {
            return false;
        }

        message.deliveryState = QStringLiteral("sent");
        historyMessages.append(message);
    }

    const int addedCount = mergeHistoryMessages(historyMessages);
    const int pageLimit = json.value(QStringLiteral("limit")).toInt(50);
    emit historyMessagesLoaded(QStringLiteral("private"),
                               peerUserId,
                               addedCount,
                               messages.size() >= pageLimit);
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
    message.createdAt = formatMessageTimeForDisplay(
        json.value(QStringLiteral("created_at")).toString());
    message.isMine = json.value(QStringLiteral("is_mine")).toBool(false);

    const bool isGroup = message.chatType == QStringLiteral("group");
    const bool isPrivate = message.chatType == QStringLiteral("private") &&
                           message.receiverUserId.toLongLong() > 0;
    return !message.messageId.isEmpty() && !message.clientMsgId.isEmpty() &&
           !message.meetingId.isEmpty() && (isGroup || isPrivate) &&
           message.senderUserId > 0 && !message.senderName.isEmpty() &&
           !message.content.isEmpty() && !message.createdAt.isEmpty();
}

bool ChatController::belongsToConversation(const ChatMessage &message,
                                           const QString &chatType,
                                           const QString &peerUserId)
{
    if (chatType == QStringLiteral("group")) {
        return message.chatType == QStringLiteral("group");
    }

    if (chatType != QStringLiteral("private") || peerUserId.isEmpty() ||
        message.chatType != QStringLiteral("private")) {
        return false;
    }

    return QString::number(message.senderUserId) == peerUserId ||
           message.receiverUserId == peerUserId;
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

int ChatController::findMessageByServerId(const QString &messageId) const
{
    if (messageId.isEmpty()) {
        return -1;
    }

    for (int row = 0; row < m_messages.size(); ++row) {
        if (m_messages.at(row).messageId == messageId) {
            return row;
        }
    }
    return -1;
}

int ChatController::mergeHistoryMessages(const QVector<ChatMessage> &messages)
{
    if (messages.isEmpty()) {
        return 0;
    }

    QVector<ChatMessage> missingMessages;
    for (const ChatMessage &message : messages) {
        if (findMessage(message.clientMsgId) < 0 &&
            findMessageByServerId(message.messageId) < 0) {
            missingMessages.append(message);
        }
    }

    if (missingMessages.isEmpty()) {
        return 0;
    }

    // 历史消息按时间正序放在已有实时消息之前，避免打开群聊后出现倒序跳动。
    beginResetModel();
    QVector<ChatMessage> merged;
    merged.reserve(missingMessages.size() + m_messages.size());
    for (const ChatMessage &message : missingMessages) {
        merged.append(message);
    }
    for (const ChatMessage &message : m_messages) {
        merged.append(message);
    }
    m_messages = std::move(merged);
    endResetModel();
    emit messagesChanged();
    return missingMessages.size();
}

void ChatController::updateMessage(int row, const ChatMessage &message)
{
    m_messages[row] = message;
    const QModelIndex modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex);
    emit messagesChanged();
}
