#include <QtTest>

#include "../src/controllers/meeting/meetingcontroller.h"

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

    void historyMeetingRequestIsEmitted()
    {
        MeetingController meetingController;
        QSignalSpy requestSpy(&meetingController, &MeetingController::historyMeetingsRequested);

        meetingController.requestHistoryMeetings();

        QCOMPARE(requestSpy.count(), 1);
    }

};

QTEST_GUILESS_MAIN(MeetingControllerTest)

#include "meetingcontroller_test.moc"
