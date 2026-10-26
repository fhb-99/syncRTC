#ifndef RECOVERYCONTROLLER_H
#define RECOVERYCONTROLLER_H

#include <QObject>
#include <QTimer>
#include "../../models/Data.h"
#include "../../models/global.h"

class MeetingController;

// 只在 Qt 主线程协调业务恢复；不拥有媒体对象，不等待 ICE 或首帧。
class RecoveryController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool recovering READ recovering NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
public:
    explicit RecoveryController(MeetingController *meeting, QObject *parent = nullptr);
    void setServer(const ServerInfo &server);
    void authenticated(const QJsonObject &response);
    bool handleMessage(RequestID id, const QJsonObject &response);
    bool ready() const { return m_state == State::Ready; }
    bool recovering() const;
    QString statusText() const { return m_statusText; }
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    void interrupted();
    void meetingRecovered(const QString &meetingId);
    void recoveryFailed(const QString &reason);

private:
    enum class State { Idle, Ready, Waiting, Connecting, Authenticating, Resuming, Failed };
    void setState(State state, const QString &text);
    void disconnected();
    void connectNow();
    void retry();
    void fail(const QString &reason);
    void finish();
    void cancelMeeting();
    void send(RequestID id, const QJsonObject &request);

    MeetingController *m_meeting;
    ServerInfo m_server;
    State m_state = State::Idle;
    QString m_statusText;
    QTimer m_retryTimer;
    QTimer m_stageTimer;
    QTimer m_budgetTimer;
    int m_failures = 0;
    bool m_authenticated = false;
    bool m_resumeSupported = false;
    QString m_requestId;
};

#endif
