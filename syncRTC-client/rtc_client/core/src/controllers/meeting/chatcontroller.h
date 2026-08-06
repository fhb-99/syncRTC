#ifndef CHATCONTROLLER_H
#define CHATCONTROLLER_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

class ChatController : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY messagesChanged)

public:
    enum MessageRole {
        MessageIdRole = Qt::UserRole + 1,
        ClientMsgIdRole,
        MeetingIdRole,
        ChatTypeRole,
        SenderUserIdRole,
        SenderNameRole,
        ReceiverUserIdRole,
        ContentRole,
        CreatedAtRole,
        DeliveryStateRole,
        IsMineRole,
    };
    Q_ENUM(MessageRole)

    explicit ChatController(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 切换会议时清空上一场会议的临时聊天记录，模型只展示当前会议消息。
    Q_INVOKABLE void setActiveMeetingId(const QString &meetingId);
    Q_INVOKABLE void sendGroupMessage(const QString &meetingId, const QString &content);
    Q_INVOKABLE void sendPrivateMessage(const QString &meetingId, qint64 targetUserId,
                                        const QString &content);
    Q_INVOKABLE void requestGroupHistory(const QString &meetingId,
                                         const QString &beforeMessageId = QString(),
                                         int limit = 50);
    Q_INVOKABLE void requestHistory(const QString &meetingId,
                                    const QString &chatType,
                                    const QString &peerUserId = QString(),
                                    const QString &beforeMessageId = QString(),
                                    int limit = 50);
    Q_INVOKABLE QString earliestMessageId(const QString &chatType,
                                          const QString &peerUserId = QString()) const;

    bool applySendMessageAck(const QJsonObject &json);
    bool applyMessageReceived(const QJsonObject &json);
    bool applyGroupHistoryResponse(const QJsonObject &json);
    bool applyPrivateHistoryResponse(const QJsonObject &json);

signals:
    void messagesChanged();
    void messageSendFailed(const QString &clientMsgId, int error);
    void groupHistoryLoadFailed(int error);
    void privateHistoryLoadFailed(const QString &peerUserId, int error);
    void historyMessagesLoaded(const QString &chatType,
                               const QString &peerUserId,
                               int addedCount,
                               bool hasMore);

private:
    struct ChatMessage {
        QString messageId;
        QString clientMsgId;
        QString meetingId;
        QString chatType;
        qint64 senderUserId = 0;
        QString senderName;
        QString receiverUserId;
        QString content;
        QString createdAt;
        QString deliveryState;
        bool isMine = false;
    };

    static bool messageFromJson(const QJsonObject &json, ChatMessage &message);
    void sendMessage(const QString &meetingId, const QString &chatType,
                     const QString &targetUserId, const QString &content);
    static bool belongsToConversation(const ChatMessage &message,
                                      const QString &chatType,
                                      const QString &peerUserId);
    int findMessage(const QString &clientMsgId) const;
    int findMessageByServerId(const QString &messageId) const;
    int mergeHistoryMessages(const QVector<ChatMessage> &messages);
    void updateMessage(int row, const ChatMessage &message);

    QString m_activeMeetingId;
    QVector<ChatMessage> m_messages;
};

#endif // CHATCONTROLLER_H
