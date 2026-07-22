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
        realtimeController.slot_message_recv(AUTH_LOGIN_RESPONSE, response);

        QCOMPARE(currentUser.username(), QStringLiteral("测试用户"));
        QCOMPARE(currentUser.email(), QStringLiteral("tester@example.com"));
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
};

QTEST_MAIN(RealtimeControllerTest)

#include "realtimecontroller_test.moc"
