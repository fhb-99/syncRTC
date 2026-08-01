#include <QJsonDocument>
#include <QSignalSpy>
#include <QtTest>

#include "../src/controllers/meeting/chatcontroller.h"
#include "../src/network/tcpmgr.h"

class ChatControllerTest : public QObject
{
    Q_OBJECT

private:
    static QJsonObject messagePayload(const QString &clientMsgId)
    {
        return {
            {"message_id", "server-1"},
            {"client_msg_id", clientMsgId},
            {"meeting_id", "100001"},
            {"chat_type", "group"},
            {"sender_user_id", 7},
            {"sender_name", "测试用户"},
            {"receiver_user_id", QJsonValue(QJsonValue::Null)},
            {"content", "大家好"},
            {"created_at", "10:30"},
        };
    }

private slots:
    void groupMessageSendsRequestAndAddsSendingMessage()
    {
        ChatController controller;
        controller.setActiveMeetingId("100001");
        QSignalSpy sendDataSpy(TcpMgr::GetInstance().get(), &TcpMgr::signal_send_data);

        controller.sendGroupMessage("100001", " 大家好 ");

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), static_cast<int>(ID_SEND_MEETING_MESSAGE_REQUEST));
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("meeting_id").toString(), QStringLiteral("100001"));
        QCOMPARE(request.value("chat_type").toString(), QStringLiteral("group"));
        QVERIFY(request.value("target_user_id").isNull());
        QCOMPARE(request.value("content").toString(), QStringLiteral("大家好"));
        QVERIFY(!request.value("client_msg_id").toString().isEmpty());

        QCOMPARE(controller.rowCount(), 1);
        const QModelIndex first = controller.index(0, 0);
        QCOMPARE(controller.data(first, ChatController::DeliveryStateRole).toString(), QStringLiteral("sending"));
        QVERIFY(controller.data(first, ChatController::IsMineRole).toBool());
    }

    void successAckMarksLocalMessageSent()
    {
        ChatController controller;
        controller.setActiveMeetingId("100001");
        controller.sendGroupMessage("100001", "大家好");
        const QString clientMsgId = controller.data(
            controller.index(0, 0), ChatController::ClientMsgIdRole).toString();

        QJsonObject ack = messagePayload(clientMsgId);
        ack["error"] = ErrorCodes::SUCCESS;
        QVERIFY(controller.applySendMessageAck(ack));

        const QModelIndex first = controller.index(0, 0);
        QCOMPARE(controller.data(first, ChatController::MessageIdRole).toString(), QStringLiteral("server-1"));
        QCOMPARE(controller.data(first, ChatController::DeliveryStateRole).toString(), QStringLiteral("sent"));
    }

    void failedAckMarksLocalMessageFailed()
    {
        ChatController controller;
        controller.setActiveMeetingId("100001");
        controller.sendGroupMessage("100001", "大家好");
        const QString clientMsgId = controller.data(
            controller.index(0, 0), ChatController::ClientMsgIdRole).toString();
        QSignalSpy failedSpy(&controller, &ChatController::messageSendFailed);

        QVERIFY(controller.applySendMessageAck(QJsonObject{
            {"error", ErrorCodes::ERROR_MEETING_STATUS},
            {"client_msg_id", clientMsgId},
        }));

        QCOMPARE(controller.data(controller.index(0, 0), ChatController::DeliveryStateRole).toString(),
                 QStringLiteral("failed"));
        QCOMPARE(failedSpy.count(), 1);
    }

    void groupPushAddsOtherUsersMessage()
    {
        ChatController controller;
        controller.setActiveMeetingId("100001");

        QVERIFY(controller.applyMessageReceived(messagePayload("remote-1")));

        QCOMPARE(controller.rowCount(), 1);
        const QModelIndex first = controller.index(0, 0);
        QCOMPARE(controller.data(first, ChatController::SenderNameRole).toString(), QStringLiteral("测试用户"));
        QCOMPARE(controller.data(first, ChatController::DeliveryStateRole).toString(), QStringLiteral("sent"));
        QVERIFY(!controller.data(first, ChatController::IsMineRole).toBool());
    }

    void privateMessageSendsTargetAndMarksSent()
    {
        ChatController controller;
        controller.setActiveMeetingId("100001");
        QSignalSpy sendDataSpy(TcpMgr::GetInstance().get(), &TcpMgr::signal_send_data);

        controller.sendPrivateMessage("100001", 9, "单独确认一下");

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("chat_type").toString(), QStringLiteral("private"));
        QCOMPARE(request.value("target_user_id").toInteger(), 9);
        QCOMPARE(controller.data(controller.index(0, 0), ChatController::ReceiverUserIdRole).toString(),
                 QStringLiteral("9"));

        const QString clientMsgId = controller.data(
            controller.index(0, 0), ChatController::ClientMsgIdRole).toString();
        QJsonObject ack = messagePayload(clientMsgId);
        ack["error"] = ErrorCodes::SUCCESS;
        ack["chat_type"] = "private";
        ack["receiver_user_id"] = 9;
        ack["content"] = "单独确认一下";
        QVERIFY(controller.applySendMessageAck(ack));

        QCOMPARE(controller.data(controller.index(0, 0), ChatController::DeliveryStateRole).toString(),
                 QStringLiteral("sent"));
    }
};

QTEST_GUILESS_MAIN(ChatControllerTest)

#include "chatcontroller_test.moc"
