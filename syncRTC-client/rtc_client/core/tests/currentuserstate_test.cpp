#include <QSignalSpy>
#include <QtTest>

#include "../src/models/currentuserstate.h"

class CurrentUserStateTest : public QObject
{
    Q_OBJECT

private slots:
    void profilePropertiesNotifyQmlBindings()
    {
        CurrentUserState currentUser;
        QSignalSpy usernameSpy(&currentUser, &CurrentUserState::usernameChanged);
        QSignalSpy emailSpy(&currentUser, &CurrentUserState::emailChanged);

        currentUser.setUsername("\u6d4b\u8bd5\u7528\u6237");
        currentUser.setEmail("tester@example.com");

        QCOMPARE(currentUser.property("username").toString(), QStringLiteral("\u6d4b\u8bd5\u7528\u6237"));
        QCOMPARE(currentUser.property("email").toString(), QStringLiteral("tester@example.com"));
        QCOMPARE(usernameSpy.count(), 1);
        QCOMPARE(emailSpy.count(), 1);
    }
};

QTEST_MAIN(CurrentUserStateTest)

#include "currentuserstate_test.moc"
