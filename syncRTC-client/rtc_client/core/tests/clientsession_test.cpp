#include <QtTest>

#include "../src/models/clientsession.h"

class ClientSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void storesDeviceIdAndSessionToken()
    {
        ClientSession session;
        session.setDeviceId(QStringLiteral("device-id"));
        session.setSessionToken(QStringLiteral("session-token"));

        QCOMPARE(session.getDeviceId(), QStringLiteral("device-id"));
        QCOMPARE(session.getSessionToken(), QStringLiteral("session-token"));
    }
};

QTEST_GUILESS_MAIN(ClientSessionTest)

#include "clientsession_test.moc"
