#include <QtTest>
#include <QJsonDocument>

#include "../src/controllers/meeting/meetingcontroller.h"
#include "../src/network/tcpmgr.h"

class MeetingControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void recentMeetingsBecomeQmlRoles()
    {
        MeetingController meetingController;
        const QJsonArray meetings{
            QJsonObject{
                {"meeting_id", "100001"},
                {"title", "媒体转发服务评审"},
                {"schedule", "今天 14:00"},
                {"participant_count", 4},
                {"status", "in_progress"},
            },
            QJsonObject{
                {"meeting_id", "100002"},
                {"title", "AI 助手接口联调"},
                {"schedule", "明天 10:30"},
                {"participant_count", 6},
                {"status", "scheduled"},
            },
        };

        QVERIFY(meetingController.applyRecentMeeting(meetings));
        QCOMPARE(meetingController.rowCount(), 2);

        const QModelIndex first = meetingController.index(0, 0);
        QCOMPARE(meetingController.data(first, MeetingController::MeetingIdRole).toString(),
                 QStringLiteral("100001"));
        QCOMPARE(meetingController.data(first, MeetingController::TitleRole).toString(),
                 QStringLiteral("媒体转发服务评审"));
        QCOMPARE(meetingController.data(first, MeetingController::ParticipantsRole).toString(),
                 QStringLiteral("4 人"));
        QCOMPARE(meetingController.data(first, MeetingController::StatusRole).toString(),
                 QStringLiteral("进行中"));
    }

    void emptyServerResponseMarksRecentMeetingsLoaded()
    {
        MeetingController meetingController;

        QVERIFY(!meetingController.recentMeetingsLoaded());
        QVERIFY(meetingController.applyRecentMeeting({}));
        QVERIFY(meetingController.recentMeetingsLoaded());
        QCOMPARE(meetingController.count(), 0);
    }

    void emptyMeetingCodeCannotBeCopied()
    {
        MeetingController meetingController;

        QVERIFY(!meetingController.copyMeetingCode(QStringLiteral("   ")));
    }

    void joinMeetingRequestSendsMeetingCode()
    {
        MeetingController meetingController;
        const std::shared_ptr<TcpMgr> tcpMgr = TcpMgr::GetInstance();
        QSignalSpy sendDataSpy(tcpMgr.get(), &TcpMgr::signal_send_data);

        meetingController.requestJoinMeeting(QStringLiteral("75231032"));

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), static_cast<int>(ID_JOIN_MEETING_REQUEST));
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("meeting_code").toString(), QStringLiteral("75231032"));
    }

    void startMeetingRequestSendsMeetingId()
    {
        MeetingController meetingController;
        const std::shared_ptr<TcpMgr> tcpMgr = TcpMgr::GetInstance();
        QSignalSpy sendDataSpy(tcpMgr.get(), &TcpMgr::signal_send_data);

        meetingController.requestStartMeeting(QStringLiteral("100001"));

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), static_cast<int>(ID_START_MEETING_REQUEST));
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("meeting_id").toString(), QStringLiteral("100001"));
    }

    void leaveMeetingRequestSendsMeetingId()
    {
        MeetingController meetingController;
        const std::shared_ptr<TcpMgr> tcpMgr = TcpMgr::GetInstance();
        QSignalSpy sendDataSpy(tcpMgr.get(), &TcpMgr::signal_send_data);

        meetingController.requestLeaveMeeting(QStringLiteral("100001"));

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), static_cast<int>(ID_LEAVE_MEETING_REQUEST));
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("meeting_id").toString(), QStringLiteral("100001"));
    }

    void endMeetingRequestSendsMeetingId()
    {
        MeetingController meetingController;
        const std::shared_ptr<TcpMgr> tcpMgr = TcpMgr::GetInstance();
        QSignalSpy sendDataSpy(tcpMgr.get(), &TcpMgr::signal_send_data);

        meetingController.requestEndMeeting(QStringLiteral("100001"));

        QCOMPARE(sendDataSpy.count(), 1);
        const QList<QVariant> arguments = sendDataSpy.takeFirst();
        QCOMPARE(arguments.at(0).toInt(), static_cast<int>(ID_END_MEETING_REQUEST));
        const QJsonObject request = QJsonDocument::fromJson(arguments.at(1).toByteArray()).object();
        QCOMPARE(request.value("meeting_id").toString(), QStringLiteral("100001"));
    }

};

QTEST_GUILESS_MAIN(MeetingControllerTest)

#include "meetingcontroller_test.moc"
