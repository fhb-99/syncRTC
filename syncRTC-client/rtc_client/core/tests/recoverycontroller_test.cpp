#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

#include "../src/controllers/auth/LoginController.h"
#include "../src/controllers/meeting/realtimecontroller.h"
#include "../src/models/clientsession.h"

// 只推进期限，不缩短生产定时参数；网络收发与恢复路径仍走真实 QObject/socket。
struct TcpHeartbeatTestAccess
{
    static void expire(TcpMgr &tcp, bool paused = false)
    {
        const auto now = tcp.m_heartbeatClock.elapsed();
        tcp.m_lastHeartbeatAck = now - 15001;
        tcp.m_lastHeartbeatTick = now - (paused ? 16000 : 0);
        for (auto &probe : tcp.m_probes) probe.sentAt = now - 16000;
    }
    static void tick(TcpMgr &tcp) { tcp.heartbeatTick(); }
    static void nextProbe(TcpMgr &tcp) { tcp.sendHeartbeat(tcp.m_heartbeatClock.elapsed()); }
    static qint64 ack(const TcpMgr &tcp) { return tcp.m_lastHeartbeatAck; }
    static void finishReprobe(TcpMgr &tcp) { tcp.m_reprobeUntil = tcp.m_heartbeatClock.elapsed() - 1; }
};

namespace {
QByteArray frame(RequestID id, const QJsonObject &json)
{
    const auto body = QJsonDocument(json).toJson(QJsonDocument::Compact);
    QByteArray bytes(4, '\0');
    qToBigEndian<quint16>(id, bytes.data());
    qToBigEndian<quint16>(body.size(), bytes.data() + 2);
    return bytes + body;
}

QJsonObject authResponse(bool resume = true)
{
    return {{"error", 0}, {"username", "测试用户"}, {"email", "test@example.com"},
            {"meetings", QJsonArray{}},
            {"capabilities", resume ? QJsonArray{"meeting_resume_v1"} : QJsonArray{}}};
}

QJsonObject snapshot()
{
    return {{"error", 0}, {"meeting_id", "71"}, {"meeting_code", "123456"},
            {"status", "scheduled"}, {"role", "participant"},
            {"members", QJsonArray{QJsonObject{{"user_id", 42}, {"is_self", true}},
                                  QJsonObject{{"user_id", 43}, {"name", "旧成员"}}}},
            {"resume_ticket", "opaque-ticket"}, {"membership_generation", "1"}};
}

class LocalRealtime : public QObject
{
public:
    struct Request { int id; QJsonObject json; };
    QTcpServer listener;
    QPointer<QTcpSocket> socket;
    QList<Request> requests;
    LocalRealtime()
    {
        connect(&listener, &QTcpServer::newConnection, this, [this] {
            socket = listener.nextPendingConnection();
            auto *peer = socket.data();
            connect(peer, &QTcpSocket::readyRead, this, [this, peer, buffer = QByteArray{}]() mutable {
                buffer += peer->readAll();
                while (buffer.size() >= 4) {
                    const int id = qFromBigEndian<quint16>(buffer.constData());
                    const int size = qFromBigEndian<quint16>(buffer.constData() + 2);
                    if (buffer.size() < size + 4) return;
                    requests.append({id, QJsonDocument::fromJson(buffer.mid(4, size)).object()});
                    buffer.remove(0, size + 4);
                }
            });
        });
    }
    int count(RequestID id) const
    {
        return std::count_if(requests.begin(), requests.end(), [id](const auto &r) { return r.id == id; });
    }
    QJsonObject last(RequestID id) const
    {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it)
            if (it->id == id) return it->json;
        return {};
    }
    void reply(RequestID id, const QJsonObject &json) { socket->write(frame(id, json)); }
};
}

class RecoveryControllerTest : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() { TcpMgr::GetInstance()->closeConnection(); }

    void reconnectAuthenticatesOnceAndReplacesSnapshot()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        ClientSession session;
        LoginController login(&session, nullptr, "SyncRTC-recovery-test-" + QUuid::createUuid().toString());
        CurrentUserState user;
        RealtimeController realtime(&user);
        auto recovery = realtime.recoveryController();
        auto meeting = realtime.meetingController();
        connect(&login, &LoginController::serverPrepared, recovery, &RecoveryController::setServer);
        QSignalSpy profile(&realtime, &RealtimeController::profileReady);
        QSignalSpy joined(meeting, &MeetingController::joinMeetingSucceeded);
        QSignalSpy restored(meeting, &MeetingController::meetingRestored);
        QSignalSpy interrupted(recovery, &RecoveryController::interrupted);
        const QJsonObject gate{{"error", 0}, {"uid", 42}, {"email", "test@example.com"},
                              {"host", "127.0.0.1"}, {"port", QString::number(server.listener.serverPort())},
                              {"session_token", "in-memory-token"}};
        login.slot_login_mod_finish(ID_LOGIN_USER, QJsonDocument(gate).toJson(), ErrorCodes::SUCCESS);
        QTRY_COMPARE(server.count(AUTH_LOGIN_REQUEST), 1);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_COMPARE(profile.count(), 1);
        meeting->requestJoinMeeting("123456");
        QTRY_COMPARE(server.count(ID_JOIN_MEETING_REQUEST), 1);
        server.reply(ID_JOIN_MEETING_RESPONSE, snapshot());
        QTRY_COMPARE(joined.count(), 1);

        // 旧连接留下一个半包，下一条连接必须从新的帧头开始解析。
        server.socket->write(frame(ID_MEETING_MEMBER_LEFT, {{"meeting_id", "71"}, {"user_id", 42}}).left(6));
        server.socket->flush();
        QTest::qWait(20);
        server.socket->abort();
        QTRY_VERIFY(recovery->recovering());
        QCOMPARE(meeting->currentMeetingId(), QString("71"));
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 2, 4000);
        QCOMPARE(server.last(AUTH_LOGIN_REQUEST).value("token").toString(), QString("in-memory-token"));
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_COMPARE(server.count(ID_RESUME_MEETING_REQUEST), 1);
        const auto firstRequest = server.last(ID_RESUME_MEETING_REQUEST);
        QVERIFY(!firstRequest.contains("password"));
        QCOMPARE(firstRequest.value("membership_generation").toString(), QString("1"));

        // 模拟服务端已经接管但响应丢失，下一次连接沿用同一 request_id。
        server.socket->abort();
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 3, 5000);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_COMPARE(server.count(ID_RESUME_MEETING_REQUEST), 2);
        QCOMPARE(server.last(ID_RESUME_MEETING_REQUEST), firstRequest);
        auto response = snapshot();
        response["request_id"] = firstRequest.value("request_id");
        response["membership_generation"] = "3";
        response["status"] = "in_progress";
        response["role"] = "host";
        response["members"] = QJsonArray{QJsonObject{{"user_id", 42}, {"is_self", true}}};
        server.reply(ID_RESUME_MEETING_RESPONSE, response);
        QTRY_VERIFY(recovery->ready());
        QCOMPARE(profile.count(), 1);
        QCOMPARE(joined.count(), 1);
        QCOMPARE(restored.count(), 1);
        QCOMPARE(interrupted.count(), 1);
        QCOMPARE(meeting->currentMeetingMembers().size(), 1);
        QCOMPARE(meeting->membershipGeneration(), QString("3"));
        QCOMPARE(restored.first().at(1).toString(), QString("in_progress"));
        QCOMPARE(restored.first().at(2).toString(), QString("host"));
    }

    void leavingDuringResumeDiscardsLateSuccess()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        CurrentUserState user;
        RealtimeController realtime(&user);
        auto recovery = realtime.recoveryController();
        auto meeting = realtime.meetingController();
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        info.uid = 42;
        info.email = "test@example.com";
        info.sessionToken = "token";
        recovery->setServer(info);
        TcpMgr::GetInstance()->slot_tcp_connect(info);
        QTRY_VERIFY(server.socket);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_VERIFY(recovery->ready());
        meeting->requestJoinMeeting("123456");
        server.reply(ID_JOIN_MEETING_RESPONSE, snapshot());
        QTRY_COMPARE(meeting->currentMeetingId(), QString("71"));
        server.socket->abort();
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 1, 4000);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_COMPARE(server.count(ID_RESUME_MEETING_REQUEST), 1);
        auto late = snapshot();
        late["request_id"] = server.last(ID_RESUME_MEETING_REQUEST).value("request_id");
        QSignalSpy restored(meeting, &MeetingController::meetingRestored);
        meeting->requestLeaveMeeting("71");
        QVERIFY(meeting->currentMeetingId().isEmpty());
        realtime.slot_message_recv(ID_RESUME_MEETING_RESPONSE, late);
        QCOMPARE(restored.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 2, 5000);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_VERIFY(recovery->ready());
        QCOMPARE(server.count(ID_RESUME_MEETING_REQUEST), 1);
        recovery->stop();
    }

    void tokenRevocationStopsRecovery()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        CurrentUserState user;
        RealtimeController realtime(&user);
        auto recovery = realtime.recoveryController();
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        recovery->setServer(info);
        TcpMgr::GetInstance()->slot_tcp_connect(info);
        QTRY_VERIFY(server.socket);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_VERIFY(recovery->ready());
        server.socket->abort();
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 1, 4000);
        QSignalSpy failed(recovery, &RecoveryController::recoveryFailed);
        server.reply(AUTH_LOGIN_RESPONSE, {{"error", ErrorCodes::ERROR_TOKEN}});
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!recovery->ready());
        QVERIFY(!recovery->recovering());
        QTest::qWait(1300);
        QCOMPARE(server.count(AUTH_LOGIN_REQUEST), 1);
    }

    void heartbeatDetectsSilentConnection()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        CurrentUserState user;
        RealtimeController realtime(&user);
        auto recovery = realtime.recoveryController();
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        recovery->setServer(info);
        TcpMgr::GetInstance()->slot_tcp_connect(info);
        QTRY_VERIFY(server.socket);
        auto auth = authResponse();
        auth["capabilities"] = QJsonArray{"meeting_resume_v1", "heartbeat_v1", "heartbeat_v2"};
        QSignalSpy interrupted(recovery, &RecoveryController::interrupted);
        QSignalSpy lost(TcpMgr::GetInstance().get(), &TcpMgr::signal_connection_lost);
        server.reply(AUTH_LOGIN_RESPONSE, auth);
        QTRY_VERIFY(recovery->ready());
        QTRY_VERIFY(server.count(ID_HEARTBEAT_REQUEST) > 0);
        // 不关闭 socket，也不回 PONG，验证静默断网由心跳触发恢复。
        QTRY_VERIFY_WITH_TIMEOUT(recovery->recovering(), 21000);
        server.socket->abort(); // 与超时相邻的 EOF 不得再启动一条恢复流程。
        QCOMPARE(interrupted.count(), 1);
        QCOMPARE(lost.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 1, 4000);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_VERIFY(recovery->ready());
        QCOMPARE(interrupted.count(), 1);
        recovery->stop();
    }

    void heartbeatRequiresTimelyMatchingSequence()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        auto tcp = TcpMgr::GetInstance();
        QSignalSpy connected(tcp.get(), &TcpMgr::signal_connect_success);
        QSignalSpy lost(tcp.get(), &TcpMgr::signal_connection_lost);
        QSignalSpy business(tcp.get(), &TcpMgr::signal_message_recv);
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        tcp->slot_tcp_connect(info);
        QTRY_COMPARE(connected.count(), 1);
        QCOMPARE(server.count(ID_HEARTBEAT_REQUEST), 0); // 鉴权前尚未启用。
        tcp->startHeartbeat(true);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 1);
        const auto first = server.last(ID_HEARTBEAT_REQUEST);
        server.reply(ID_HEARTBEAT_RESPONSE, {{"seq", "unsolicited"}});
        QTest::qWait(30);
        QCOMPARE(tcp->heartbeatRttMs(), -1);
        server.reply(ID_HEARTBEAT_RESPONSE, first);
        QTRY_VERIFY(tcp->heartbeatRttMs() >= 0);
        const auto ack = TcpHeartbeatTestAccess::ack(*tcp);
        TcpHeartbeatTestAccess::nextProbe(*tcp);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 2);
        server.reply(ID_HEARTBEAT_RESPONSE, first); // 重复回包不能刷新新探测的期限。
        QTest::qWait(30);
        QCOMPARE(TcpHeartbeatTestAccess::ack(*tcp), ack);
        TcpHeartbeatTestAccess::expire(*tcp);
        server.reply(ID_HEARTBEAT_RESPONSE, server.last(ID_HEARTBEAT_REQUEST));
        QTest::qWait(30); // 已过期但序号正确的回包同样不能续命。
        TcpHeartbeatTestAccess::tick(*tcp);
        QCOMPARE(lost.count(), 1);
        TcpHeartbeatTestAccess::tick(*tcp);
        QCOMPARE(lost.count(), 1);
        QCOMPARE(business.count(), 0);
    }

    void heartbeatPauseAllowsOnlyOneBoundedReprobe()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        auto tcp = TcpMgr::GetInstance();
        QSignalSpy connected(tcp.get(), &TcpMgr::signal_connect_success);
        QSignalSpy lost(tcp.get(), &TcpMgr::signal_connection_lost);
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        tcp->slot_tcp_connect(info);
        QTRY_COMPARE(connected.count(), 1);
        tcp->startHeartbeat(true);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 1);
        const auto oldProbe = server.last(ID_HEARTBEAT_REQUEST);
        TcpHeartbeatTestAccess::expire(*tcp, true);
        TcpHeartbeatTestAccess::tick(*tcp);
        QCOMPARE(lost.count(), 0);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 2);
        server.reply(ID_HEARTBEAT_RESPONSE, oldProbe);
        QTest::qWait(30);
        QCOMPARE(tcp->heartbeatRttMs(), -1);
        server.reply(ID_HEARTBEAT_RESPONSE, server.last(ID_HEARTBEAT_REQUEST));
        QTRY_VERIFY(tcp->heartbeatRttMs() >= 0);
        TcpHeartbeatTestAccess::expire(*tcp, true);
        TcpHeartbeatTestAccess::tick(*tcp);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 3);
        TcpHeartbeatTestAccess::expire(*tcp, true);
        TcpHeartbeatTestAccess::finishReprobe(*tcp);
        TcpHeartbeatTestAccess::tick(*tcp);
        QCOMPARE(lost.count(), 1); // 再次长暂停也不能无限延期。
    }

    void heartbeatRunsWhileMeetingSnapshotIsPending()
    {
        LocalRealtime server;
        QVERIFY(server.listener.listen(QHostAddress::LocalHost));
        CurrentUserState user;
        RealtimeController realtime(&user);
        auto recovery = realtime.recoveryController();
        ServerInfo info;
        info.host = "127.0.0.1";
        info.port = QString::number(server.listener.serverPort());
        recovery->setServer(info);
        TcpMgr::GetInstance()->slot_tcp_connect(info);
        QTRY_VERIFY(server.socket);
        server.reply(AUTH_LOGIN_RESPONSE, authResponse());
        QTRY_VERIFY(recovery->ready());
        realtime.meetingController()->requestJoinMeeting("123456");
        server.reply(ID_JOIN_MEETING_RESPONSE, snapshot());
        QTRY_COMPARE(realtime.meetingController()->currentMeetingId(), QString("71"));
        server.socket->abort();
        QTRY_COMPARE_WITH_TIMEOUT(server.count(AUTH_LOGIN_REQUEST), 1, 4000);
        auto auth = authResponse();
        auth["capabilities"] = QJsonArray{"meeting_resume_v1", "heartbeat_v2"};
        server.reply(AUTH_LOGIN_RESPONSE, auth);
        QTRY_COMPARE(server.count(ID_RESUME_MEETING_REQUEST), 1);
        QTRY_COMPARE(server.count(ID_HEARTBEAT_REQUEST), 1);
        QVERIFY(recovery->recovering());
        server.reply(ID_HEARTBEAT_RESPONSE, server.last(ID_HEARTBEAT_REQUEST));
        QTRY_VERIFY(TcpMgr::GetInstance()->heartbeatRttMs() >= 0);
        QVERIFY(recovery->recovering());
        recovery->stop();
        TcpHeartbeatTestAccess::tick(*TcpMgr::GetInstance());
        QCOMPARE(server.count(ID_HEARTBEAT_REQUEST), 1);
    }
};

QTEST_MAIN(RecoveryControllerTest)
#include "recoverycontroller_test.moc"
