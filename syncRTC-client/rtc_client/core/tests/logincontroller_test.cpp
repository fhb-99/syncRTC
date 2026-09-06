#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUuid>
#include <QtTest>

#include "../src/controllers/auth/LoginController.h"
#include "../src/models/clientsession.h"
#include "../src/models/sessioncredentialstore.h"
#include "../src/network/httpmgr.h"

namespace {
class LocalLoginServer
{
public:
    LocalLoginServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this]() {
            QTcpSocket *socket = m_server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, [this, socket]() {
                receiveRequest(socket);
            });
        });
    }

    bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    quint16 port() const
    {
        return m_server.serverPort();
    }

    void setResponse(const QJsonObject &response)
    {
        m_response = response;
    }

    void closeWithoutResponse()
    {
        m_closeWithoutResponse = true;
    }

    bool receivedRequest() const
    {
        return m_receivedRequest;
    }

    QJsonObject requestBody() const
    {
        return m_requestBody;
    }

private:
    void receiveRequest(QTcpSocket *socket)
    {
        m_requestBuffer.append(socket->readAll());

        const int headerEnd = m_requestBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return;
        }

        const QByteArray headers = m_requestBuffer.left(headerEnd);
        const QRegularExpression contentLengthExpression(
            QStringLiteral("Content-Length: (\\d+)"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = contentLengthExpression.match(
            QString::fromLatin1(headers));
        if (!match.hasMatch()) {
            return;
        }

        const int contentLength = match.captured(1).toInt();
        const int bodyOffset = headerEnd + 4;
        if (m_requestBuffer.size() < bodyOffset + contentLength) {
            return;
        }

        const QByteArray body = m_requestBuffer.mid(bodyOffset, contentLength);
        m_requestBody = QJsonDocument::fromJson(body).object();
        m_receivedRequest = true;

        if (m_closeWithoutResponse) {
            socket->disconnectFromHost();
            return;
        }

        const QByteArray responseBody = QJsonDocument(m_response).toJson(QJsonDocument::Compact);
        const QByteArray response = "HTTP/1.1 200 OK\r\n"
                                    "Content-Type: application/json\r\n"
                                    "Content-Length: " + QByteArray::number(responseBody.size()) +
                                    "\r\nConnection: close\r\n\r\n" + responseBody;
        socket->write(response);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QByteArray m_requestBuffer;
    QJsonObject m_response;
    QJsonObject m_requestBody;
    bool m_closeWithoutResponse = false;
    bool m_receivedRequest = false;
};

QJsonObject successfulLoginResponse(quint16 realtimePort, const QString &sessionToken)
{
    QJsonObject response;
    response["error"] = ErrorCodes::SUCCESS;
    response["email"] = "alice@example.com";
    response["host"] = "127.0.0.1";
    response["port"] = QString::number(realtimePort);
    response["uid"] = 42;
    response["session_token"] = sessionToken;
    return response;
}

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

class GateServerUrlRestore
{
public:
    GateServerUrlRestore()
        : m_previousUrl(GateServer_URL)
    {
    }

    ~GateServerUrlRestore()
    {
        GateServer_URL = m_previousUrl;
    }

private:
    QString m_previousUrl;
};
}

class LoginControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void passwordLoginWithRememberingSavesReturnedSession()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        LocalLoginServer gateServer;
        QVERIFY(gateServer.listen());

        QTcpServer realtimeServer;
        QVERIFY(realtimeServer.listen(QHostAddress::LocalHost, 0));
        gateServer.setResponse(successfulLoginResponse(realtimeServer.serverPort(),
                                                        QStringLiteral("saved-session-token")));

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.clear());

        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:%1").arg(gateServer.port());

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        controller.setDeviceID(QStringLiteral("device-id"));
        QObject::connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
                         &controller, &LoginController::slot_login_mod_finish);

        controller.LoginRequest(QStringLiteral("alice@example.com"),
                                QStringLiteral("Passw0rd"),
                                true);

        QTRY_VERIFY(gateServer.receivedRequest());
        const QJsonObject request = gateServer.requestBody();
        QCOMPARE(request.value("account").toString(), QStringLiteral("alice@example.com"));
        QCOMPARE(request.value("password").toString(), QStringLiteral("Passw0rd"));
        QCOMPARE(request.value("device_id").toString(), QStringLiteral("device-id"));
        QVERIFY(!request.contains("session_token"));

        QString account;
        QString sessionToken;
        QTRY_VERIFY(store.load(&account, &sessionToken));
        QCOMPARE(account, QStringLiteral("alice@example.com"));
        QCOMPARE(sessionToken, QStringLiteral("saved-session-token"));
        QCOMPARE(clientSession.getSessionToken(), QStringLiteral("saved-session-token"));
    }

    void rememberedSessionLoginSendsNoPasswordAndRefreshesSession()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        LocalLoginServer gateServer;
        QVERIFY(gateServer.listen());

        QTcpServer realtimeServer;
        QVERIFY(realtimeServer.listen(QHostAddress::LocalHost, 0));
        gateServer.setResponse(successfulLoginResponse(realtimeServer.serverPort(),
                                                        QStringLiteral("refreshed-session-token")));

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.save(QStringLiteral("alice@example.com"),
                           QStringLiteral("stored-session-token")));

        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:%1").arg(gateServer.port());

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        controller.setDeviceID(QStringLiteral("device-id"));
        QObject::connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
                         &controller, &LoginController::slot_login_mod_finish);

        controller.ResumeLoginRequest();

        QTRY_VERIFY(gateServer.receivedRequest());
        const QJsonObject request = gateServer.requestBody();
        QCOMPARE(request.value("session_token").toString(), QStringLiteral("stored-session-token"));
        QCOMPARE(request.value("device_id").toString(), QStringLiteral("device-id"));
        QVERIFY(!request.contains("account"));
        QVERIFY(!request.contains("password"));

        QString account;
        QString sessionToken;
        QTRY_VERIFY(store.load(&account, &sessionToken));
        QCOMPARE(sessionToken, QStringLiteral("refreshed-session-token"));
        QCOMPARE(clientSession.getSessionToken(), QStringLiteral("refreshed-session-token"));
    }

    void passwordLoginWithoutRememberingClearsExistingSession()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        LocalLoginServer gateServer;
        QVERIFY(gateServer.listen());

        QTcpServer realtimeServer;
        QVERIFY(realtimeServer.listen(QHostAddress::LocalHost, 0));
        gateServer.setResponse(successfulLoginResponse(realtimeServer.serverPort(),
                                                        QStringLiteral("new-session-token")));

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.save(QStringLiteral("old@example.com"),
                           QStringLiteral("old-session-token")));

        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:%1").arg(gateServer.port());

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        controller.setDeviceID(QStringLiteral("device-id"));
        QObject::connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
                         &controller, &LoginController::slot_login_mod_finish);

        controller.LoginRequest(QStringLiteral("alice@example.com"),
                                QStringLiteral("Passw0rd"),
                                false);

        QTRY_VERIFY(gateServer.receivedRequest());
        QString account;
        QString sessionToken;
        QTRY_VERIFY(!store.load(&account, &sessionToken));
    }

    void invalidRememberedSessionIsCleared()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        LocalLoginServer gateServer;
        QVERIFY(gateServer.listen());
        gateServer.setResponse(QJsonObject{{"error", static_cast<int>(ErrorCodes::ERROR_SESSION_INVALID)}});

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.save(QStringLiteral("alice@example.com"),
                           QStringLiteral("stored-session-token")));

        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:%1").arg(gateServer.port());

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        controller.setDeviceID(QStringLiteral("device-id"));
        QObject::connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
                         &controller, &LoginController::slot_login_mod_finish);

        controller.ResumeLoginRequest();

        QTRY_VERIFY(gateServer.receivedRequest());
        QTRY_VERIFY(!controller.hasRememberedSession());

        QString account;
        QString sessionToken;
        QVERIFY(!store.load(&account, &sessionToken));
    }

    void forgettingRememberedSessionClearsControllerState()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.save(QStringLiteral("alice@example.com"),
                           QStringLiteral("stored-session-token")));

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        QVERIFY(controller.hasRememberedSession());

        controller.ForgetRememberedSession();

        QVERIFY(!controller.hasRememberedSession());
        QString account;
        QString sessionToken;
        QVERIFY(!store.load(&account, &sessionToken));
    }

    void networkFailureKeepsRememberedSession()
    {
#ifndef Q_OS_WIN
        QSKIP("Windows Credential Manager is unavailable on this platform");
#endif

        LocalLoginServer gateServer;
        QVERIFY(gateServer.listen());
        gateServer.closeWithoutResponse();

        const QString targetName = QStringLiteral("SyncRTC/rtc_client/tests/%1")
                                       .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
        SessionCredentialStore store(targetName);
        const CredentialCleanup cleanup(store);
        QVERIFY(store.save(QStringLiteral("alice@example.com"),
                           QStringLiteral("stored-session-token")));

        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:%1").arg(gateServer.port());

        ClientSession clientSession;
        LoginController controller(&clientSession, nullptr, targetName);
        controller.setDeviceID(QStringLiteral("device-id"));
        QObject::connect(HttpMgr::GetInstance().get(), &HttpMgr::signal_login_mod_finish,
                         &controller, &LoginController::slot_login_mod_finish);
        QSignalSpy loginFailedSpy(&controller, &LoginController::loginFailed);

        controller.ResumeLoginRequest();

        QTRY_VERIFY(gateServer.receivedRequest());
        QTRY_COMPARE(loginFailedSpy.count(), 1);

        QString account;
        QString sessionToken;
        QVERIFY(store.load(&account, &sessionToken));
        QCOMPARE(sessionToken, QStringLiteral("stored-session-token"));
    }
};

QTEST_GUILESS_MAIN(LoginControllerTest)

#include "logincontroller_test.moc"
