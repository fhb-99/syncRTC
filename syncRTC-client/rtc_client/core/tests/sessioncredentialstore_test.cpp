#include <QUuid>
#include <QtTest>

#include "../src/models/sessioncredentialstore.h"

namespace {
class CredentialCleanup
{
public:
    explicit CredentialCleanup(SessionCredentialStore &store)
        : m_store(store)
    {
    }

    ~CredentialCleanup()
    {
        m_store.clear();
    }

private:
    SessionCredentialStore &m_store;
};
}

class SessionCredentialStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void savesReadsOverwritesAndClearsSession()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);

        QVERIFY(store.clear());
        QVERIFY(store.save(QStringLiteral("alice@example.com"),
                           QStringLiteral("session-token-one")));

        QString account;
        QString sessionToken;
        QVERIFY(store.load(&account, &sessionToken));
        QCOMPARE(account, QStringLiteral("alice@example.com"));
        QCOMPARE(sessionToken, QStringLiteral("session-token-one"));

        QVERIFY(store.save(QStringLiteral("bob@example.com"),
                           QStringLiteral("session-token-two")));
        QVERIFY(store.load(&account, &sessionToken));
        QCOMPARE(account, QStringLiteral("bob@example.com"));
        QCOMPARE(sessionToken, QStringLiteral("session-token-two"));

        QVERIFY(store.clear());
        QVERIFY(!store.load(&account, &sessionToken));
        QCOMPARE(account, QString());
        QCOMPARE(sessionToken, QString());
    }

    void rejectsEmptyAccountOrSessionToken()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);

        QVERIFY(!store.save(QString(), QStringLiteral("session-token")));
        QVERIFY(!store.save(QStringLiteral("alice@example.com"), QString()));
    }
};

QTEST_GUILESS_MAIN(SessionCredentialStoreTest)

#include "sessioncredentialstore_test.moc"
