#include <QSignalSpy>
#include <QtTest>

#include "../src/controllers/meeting/realtimecontroller.h"

class RealtimeControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void loginResponseRoutesToProfileController()
    {
        CurrentUserState currentUser;
        RealtimeController realtimeController(&currentUser);
        QSignalSpy profileReadySpy(&realtimeController, &RealtimeController::profileReady);

        QJsonObject response;
        response["error"] = ErrorCodes::SUCCESS;
        response["username"] = "测试用户";
        response["email"] = "tester@example.com";
        response["meetings"] = QJsonArray{
            QJsonObject{
                {"meeting_id", "100001"},
                {"title", "媒体转发服务评审"},
                {"schedule", "今天 14:00"},
                {"participant_count", 4},
                {"status", "in_progress"},
            },
        };
        realtimeController.slot_message_recv(AUTH_LOGIN_RESPONSE, response);

        QCOMPARE(currentUser.username(), QStringLiteral("测试用户"));
        QCOMPARE(currentUser.email(), QStringLiteral("tester@example.com"));
        QCOMPARE(realtimeController.meetingController()->rowCount(), 1);
        QCOMPARE(profileReadySpy.count(), 1);
    }

    void unknownMessageDoesNotChangeProfile()
    {
        CurrentUserState currentUser;
        RealtimeController realtimeController(&currentUser);
        QSignalSpy profileReadySpy(&realtimeController, &RealtimeController::profileReady);

        realtimeController.slot_message_recv(ID_LOGIN_USER, QJsonObject{});

        QCOMPARE(currentUser.username(), QString());
        QCOMPARE(currentUser.email(), QString());
        QCOMPARE(profileReadySpy.count(), 0);
    }

    void createMeetingResponseRefreshesRecentMeetings()
    {
        CurrentUserState currentUser;
        RealtimeController realtimeController(&currentUser);

        const QJsonObject response{
            {"error", ErrorCodes::SUCCESS},
            {"meetings", QJsonArray{
                QJsonObject{
                    {"meeting_code", "200001"},
                    {"title", "产品需求评审会"},
                    {"scheduled_at", "2026-07-24 14:30"},
                    {"participant_count", 1},
                    {"status", "scheduled"},
                },
            }},
        };

        realtimeController.slot_message_recv(ID_CREATE_MEETING_RESPONSE, response);

        QCOMPARE(realtimeController.meetingController()->rowCount(), 1);
        const QModelIndex first = realtimeController.meetingController()->index(0, 0);
        QCOMPARE(realtimeController.meetingController()
                     ->data(first, MeetingController::MeetingIdRole)
                     .toString(),
                 QStringLiteral("200001"));
    }

    void failedCreateMeetingResponseDoesNotTriggerLoginFailure()
    {
        CurrentUserState currentUser;
        RealtimeController realtimeController(&currentUser);
        QSignalSpy loginFailedSpy(&realtimeController, &RealtimeController::loginFailed);

        realtimeController.slot_message_recv(
            ID_CREATE_MEETING_RESPONSE, QJsonObject{{"error", ErrorCodes::ERROR_MYSQL}});

        QCOMPARE(loginFailedSpy.count(), 0);
        QCOMPARE(realtimeController.meetingController()->rowCount(), 0);
    }

    void historyMeetingResponseKeepsRecentMeetingsIntact()
    {
        CurrentUserState currentUser;
        RealtimeController realtimeController(&currentUser);
        MeetingController *meetingController = realtimeController.meetingController();

        QVERIFY(meetingController->applyRecentMeeting(QJsonArray{
            QJsonObject{
                {"meeting_code", "100001"},
                {"title", "首页最近会议"},
                {"schedule", "今天 14:00"},
                {"participant_count", 4},
                {"status", "in_progress"},
            },
        }));

        const QJsonObject response{
            {"error", ErrorCodes::SUCCESS},
            {"meetings", QJsonArray{
                QJsonObject{
                    {"meeting_code", "300001"},
                    {"title", "客户端复盘会"},
                    {"creator_display_name", "张三"},
                    {"creator_avatar_url", "https://example.com/avatar-1.png"},
                    {"started_at", "2026-07-23 16:00"},
                    {"ended_at", "2026-07-23 17:15"},
                },
                QJsonObject{
                    {"meeting_code", "300002"},
                    {"title", "项目周会"},
                    {"creator_display_name", "李四"},
                    {"creator_avatar_url", ""},
                    {"started_at", "2026-07-21 10:00"},
                    {"ended_at", "2026-07-21 11:00"},
                },
            }},
        };

        realtimeController.slot_message_recv(ID_PAST_MEETING_RESPONSE, response);

        QCOMPARE(meetingController->rowCount(), 1);
        const QModelIndex first = meetingController->index(0, 0);
        QCOMPARE(meetingController->data(first, MeetingController::MeetingIdRole)
                     .toString(),
                 QStringLiteral("100001"));
        QCOMPARE(meetingController->historyMeetings().size(), 2);
        const QVariantMap firstHistory = meetingController->historyMeetings().first().toMap();
        QCOMPARE(firstHistory.value("meetingCode").toString(),
                 QStringLiteral("300001"));
        QCOMPARE(firstHistory.value("creatorName").toString(), QStringLiteral("张三"));
        QCOMPARE(firstHistory.value("creatorAvatarUrl").toString(),
                 QStringLiteral("https://example.com/avatar-1.png"));
        QCOMPARE(firstHistory.value("startedAt").toString(), QStringLiteral("2026-07-23 16:00"));
        QCOMPARE(firstHistory.value("endedAt").toString(), QStringLiteral("2026-07-23 17:15"));
        QCOMPARE(meetingController->historyMeetings().at(1).toMap()
                     .value("meetingCode").toString(),
                 QStringLiteral("300002"));
    }
};

QTEST_MAIN(RealtimeControllerTest)

#include "realtimecontroller_test.moc"
