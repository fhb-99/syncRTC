#ifndef MEETINGCONTROLLER_H
#define MEETINGCONTROLLER_H

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>

class MeetingController : public QAbstractListModel
{
    Q_OBJECT
    // QML 通过该模型直接展示最近会议，不再依赖写死的 ListModel。
    Q_PROPERTY(int count READ count NOTIFY recentMeetingsChanged)
    // 区分“尚未收到服务端结果”和“服务端明确返回空列表”，供 QML 显示空状态。
    Q_PROPERTY(bool recentMeetingsLoaded READ recentMeetingsLoaded NOTIFY recentMeetingsChanged)
    // 历史会议仅供历史页读取，不能复用首页的最近会议模型。
    Q_PROPERTY(QVariantList historyMeetings READ historyMeetings NOTIFY historyMeetingsChanged)

public:
    enum Role {
        MeetingIdRole = Qt::UserRole + 1,
        TitleRole,
        ScheduleRole,
        ParticipantsRole,
        StatusRole,
        StatusColorRole,
    };
    Q_ENUM(Role)

    explicit MeetingController(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const;
    bool recentMeetingsLoaded() const;
    QVariantList historyMeetings() const;

    // QML 输入校验通过后调用，由会议控制器发出入会请求。
    Q_INVOKABLE void requestJoinMeeting(const QString &meetingCode);
    // 主持人点击开始会议后调用，会议权限和生命周期状态由服务端校验。
    Q_INVOKABLE void requestStartMeeting(const QString &meetingId);
    // 用户离开当前会议，服务端负责清理实时房间成员关系。
    Q_INVOKABLE void requestLeaveMeeting(const QString &meetingId);
    // 主持人结束会议，服务端校验权限并记录实际结束时间。
    Q_INVOKABLE void requestEndMeeting(const QString &meetingId);
    // QML 表单校验通过后调用，由控制器直接交给 TcpMgr 发送请求。
    Q_INVOKABLE void requestCreateMeeting(const QString &title, const QString &scheduledAt,
                                          const QString &password);
    // 进入历史会议页时调用，向服务端查询当前用户参与过的会议。
    Q_INVOKABLE void requestHistoryMeetings();
    // 会议号由服务端生成，QML 只调用该方法写入系统剪贴板。
    Q_INVOKABLE bool copyMeetingCode(const QString &meetingCode);



    // 将服务端 recent meetings JSON 原子替换为 QML 可读模型数据
    bool applyRecentMeeting(const QJsonArray &json);
    // 保持服务端返回顺序，历史页直接展示这份独立数据。
    bool applyHistoryMeetings(const QJsonArray &json);
    // 校验入会回包；只有校验成功才通知 QML 进入会议页。
    bool applyJoinMeetingResponse(const QJsonObject &json);
    // 校验开始会议回包；成功后由 QML 切换到正式会议状态。
    bool applyStartMeetingResponse(const QJsonObject &json);
    // 接收服务端广播，通知同一会议内的所有客户端刷新状态。
    bool applyMeetingStarted(const QJsonObject &json);
    bool applyLeaveMeetingResponse(const QJsonObject &json);
    bool applyEndMeetingResponse(const QJsonObject &json);
    bool applyMeetingEnded(const QJsonObject &json);
    // 收到其他用户入会通知后，增量维护当前会议成员列表。
    bool applyMeetingMemberJoined(const QJsonObject &json);
    // 收到其他用户离会通知后，从当前会议成员列表删除该用户。
    bool applyMeetingMemberLeft(const QJsonObject &json);
    // 收到断线通知后保留该成员，只更新为重连中状态。
    bool applyMeetingMemberReconnecting(const QJsonObject &json);
    // 收到重连成功通知后，把成员恢复为正常会议状态。
    bool applyMeetingMemberReconnected(const QJsonObject &json);
    QVariantList currentMeetingMembers() const;

signals:
    void recentMeetingsChanged();
    void historyMeetingsChanged();
    // 服务端确认入会成功后发出，QML 根据状态和角色渲染会议界面。
    void joinMeetingSucceeded(const QString &meetingCode, const QString &meetingId,
                              const QString &status, const QString &role,
                              const QVariantList &members);
    // 服务端拒绝入会或回包不合法时发出，QML 保持在当前页面。
    void joinMeetingFailed(int error);
    void startMeetingSucceeded(const QString &meetingId);
    void startMeetingFailed(int error);
    void meetingStarted(const QString &meetingId);
    void leaveMeetingSucceeded(const QString &meetingId);
    void leaveMeetingFailed(int error);
    void endMeetingSucceeded(const QString &meetingId);
    void endMeetingFailed(int error);
    void meetingEnded(const QString &meetingId);
    // 成员列表变化后通知 QML。首次入会和成员增量加入都会触发该信号。
    void meetingMembersChanged(const QString &meetingId, const QVariantList &members);

private:
    struct MeetingItem {
        QString meetingId;
        QString title;
        QString schedule;
        QString participants;
        QString status;
        QString statusColor;
    };

    // 最近会议和历史会议复用同一份服务端字段解析规则。
    static bool parseMeetingItem(const QJsonObject &object, MeetingItem *item);

    QVector<MeetingItem> m_recentMeetings;
    bool m_recentMeetingsLoaded = false;
    QVariantList m_historyMeetings;
    // 当前 UI 一次只能提交一个入会请求，用于将仅含 error 的成功回包关联到会议号。
    QString m_pendingJoinMeetingCode;
    QString m_pendingStartMeetingId;
    QString m_pendingLeaveMeetingId;
    QString m_pendingEndMeetingId;
    QString m_currentMeetingId;
    QVariantList m_currentMeetingMembers;
};

#endif // MEETINGCONTROLLER_H
