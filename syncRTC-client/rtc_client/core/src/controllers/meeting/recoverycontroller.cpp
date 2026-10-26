#include "recoverycontroller.h"
#include "meetingcontroller.h"
#include "../../network/tcpmgr.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QUuid>

RecoveryController::RecoveryController(MeetingController *meeting, QObject *parent)
    : QObject(parent), m_meeting(meeting)
{
    m_retryTimer.setSingleShot(true);
    m_stageTimer.setSingleShot(true);
    m_budgetTimer.setSingleShot(true);
    m_stageTimer.setInterval(5000);
    m_budgetTimer.setInterval(45000);
    connect(&m_retryTimer, &QTimer::timeout, this, &RecoveryController::connectNow);
    connect(&m_stageTimer, &QTimer::timeout, this, &RecoveryController::retry);
    connect(&m_budgetTimer, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("连接恢复超时，请重新登录后加入会议"));
    });
    auto tcp = TcpMgr::GetInstance();
    connect(tcp.get(), &TcpMgr::signal_connection_lost, this, &RecoveryController::disconnected);
    connect(tcp.get(), &TcpMgr::signal_connect_success, this, [this](bool success) {
        if (!success || m_state != State::Connecting) return;
        setState(State::Authenticating, QStringLiteral("连接已建立，正在验证登录状态…"));
        m_stageTimer.start();
        send(AUTH_LOGIN_REQUEST, {{"uid", m_server.uid}, {"email", m_server.email},
                                 {"token", m_server.sessionToken}});
    });
    connect(m_meeting, &MeetingController::recoveryCancelled, this, &RecoveryController::cancelMeeting);
}

bool RecoveryController::recovering() const
{
    return m_state == State::Waiting || m_state == State::Connecting ||
        m_state == State::Authenticating || m_state == State::Resuming;
}

void RecoveryController::setState(State state, const QString &text)
{
    qInfo() << "[Recovery] stage=" << static_cast<int>(state);
    m_state = state;
    m_statusText = text;
    TcpMgr::GetInstance()->setBusinessReady(ready());
    m_meeting->setConnectionRecovering(recovering() || state == State::Failed);
    emit stateChanged();
}

void RecoveryController::setServer(const ServerInfo &server)
{
    stop();
    m_server = server;
}

void RecoveryController::authenticated(const QJsonObject &response)
{
    m_authenticated = true;
    const auto capabilities = response.value("capabilities").toArray();
    m_resumeSupported = capabilities.contains(QStringLiteral("meeting_resume_v1"));
    finish();
    if (capabilities.contains(QStringLiteral("heartbeat_v1")) || capabilities.contains(QStringLiteral("heartbeat_v2")))
        TcpMgr::GetInstance()->startHeartbeat(capabilities.contains(QStringLiteral("heartbeat_v2")));
}

void RecoveryController::send(RequestID id, const QJsonObject &request)
{
    TcpMgr::GetInstance()->slot_send_data(id, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void RecoveryController::disconnected()
{
    if (!m_authenticated || m_state == State::Idle || m_state == State::Failed) return;
    if (m_state == State::Ready) {
        m_failures = 0;
        m_requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_budgetTimer.start();
        setState(State::Waiting, QStringLiteral("连接中断，正在恢复…"));
        m_meeting->connectionInterrupted();
        emit interrupted();
    }
    retry();
}

void RecoveryController::retry()
{
    if (!recovering()) return;
    m_stageTimer.stop();
    TcpMgr::GetInstance()->closeConnection();
    if (m_retryTimer.isActive()) return;
    const int delays[] = {1000, 2000, 4000, 8000, 10000};
    const int delay = delays[qMin(m_failures++, 4)];
    setState(State::Waiting, QStringLiteral("正在重新连接，请稍候…"));
    m_retryTimer.start(delay + QRandomGenerator::global()->bounded(251));
}

void RecoveryController::connectNow()
{
    if (m_state != State::Waiting) return;
    setState(State::Connecting, QStringLiteral("正在重新连接…"));
    TcpMgr::GetInstance()->slot_tcp_connect(m_server);
}

bool RecoveryController::handleMessage(RequestID id, const QJsonObject &response)
{
    if (id == AUTH_LOGIN_RESPONSE && recovering()) {
        if (m_state != State::Authenticating) return true;
        m_stageTimer.stop();
        const int error = response.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error == ErrorCodes::ERROR_REDIS || error == ErrorCodes::ERROR_MYSQL ||
            error == ErrorCodes::ERROR_NETWORK) {
            retry();
            return true;
        }
        if (error != ErrorCodes::SUCCESS) {
            fail(QStringLiteral("登录状态已失效，请重新登录"));
            return true;
        }
        const auto capabilities = response.value("capabilities").toArray();
        m_resumeSupported = capabilities.contains(QStringLiteral("meeting_resume_v1"));
        // 鉴权完成即启动心跳，会议快照尚在恢复时也保持传输探测。
        if (capabilities.contains(QStringLiteral("heartbeat_v1")) || capabilities.contains(QStringLiteral("heartbeat_v2")))
            TcpMgr::GetInstance()->startHeartbeat(capabilities.contains(QStringLiteral("heartbeat_v2")));
        if (m_state != State::Authenticating) return true; // 立即发送失败可能已触发重试。
        if (m_meeting->currentMeetingId().isEmpty()) {
            finish();
        } else if (!m_resumeSupported || m_meeting->resumeTicket().isEmpty()) {
            m_meeting->abandonCurrentMeeting();
            finish();
            emit recoveryFailed(QStringLiteral("服务端暂不支持恢复原会议，请重新加入"));
        } else {
            setState(State::Resuming, QStringLiteral("登录已恢复，正在同步会议…"));
            m_stageTimer.start();
            send(ID_RESUME_MEETING_REQUEST, {
                {"meeting_id", m_meeting->currentMeetingId()}, {"request_id", m_requestId},
                {"resume_ticket", m_meeting->resumeTicket()},
                {"membership_generation", m_meeting->membershipGeneration()}});
        }
        return true;
    }
    if (id == ID_RESUME_MEETING_RESPONSE) {
        if (m_state != State::Resuming) return true;
        // 路由层错误可能只有 error；成功快照必须匹配本次幂等请求。
        const int error = response.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (response.contains("request_id") && response.value("request_id").toString() != m_requestId)
            return true;
        m_stageTimer.stop();
        if (error == ErrorCodes::ERROR_REDIS || error == ErrorCodes::ERROR_MYSQL ||
            error == ErrorCodes::ERROR_NETWORK) {
            retry();
        } else if (error != ErrorCodes::SUCCESS) {
            m_meeting->abandonCurrentMeeting();
            finish();
            emit recoveryFailed(QStringLiteral("原会议已结束或恢复资格已失效，请重新加入"));
        } else if (response.value("request_id").toString() != m_requestId ||
                   !m_meeting->applyResumeMeetingResponse(response)) {
            fail(QStringLiteral("会议恢复数据无效，请重新登录后加入"));
        } else {
            finish();
            emit meetingRecovered(m_meeting->currentMeetingId());
        }
        return true;
    }
    // 恢复期间只接纳当前鉴权与快照，页面保留的数据不再接受旧增量。
    return recovering() || m_state == State::Failed;
}

void RecoveryController::finish()
{
    m_retryTimer.stop();
    m_stageTimer.stop();
    m_budgetTimer.stop();
    m_failures = 0;
    setState(State::Ready, QString());
}

void RecoveryController::cancelMeeting()
{
    if (m_state != State::Resuming) return;
    // 关闭可能已在服务端生效的恢复连接，迟到成功不能重新打开会议页。
    retry();
}

void RecoveryController::fail(const QString &reason)
{
    stop();
    setState(State::Failed, reason);
    emit recoveryFailed(reason);
}

void RecoveryController::stop()
{
    m_retryTimer.stop();
    m_stageTimer.stop();
    m_budgetTimer.stop();
    m_authenticated = false;
    m_server = ServerInfo{};
    m_requestId.clear();
    TcpMgr::GetInstance()->closeConnection();
    setState(State::Idle, QString());
    m_meeting->abandonCurrentMeeting();
}
