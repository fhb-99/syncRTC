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

signals:
    void recentMeetingsChanged();
    void historyMeetingsChanged();

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
};

#endif // MEETINGCONTROLLER_H
