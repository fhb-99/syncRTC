#ifndef MEETINGCONTROLLER_H
#define MEETINGCONTROLLER_H

#include <QAbstractListModel>
#include <QJsonArray>

class MeetingController : public QAbstractListModel
{
    Q_OBJECT
    // QML 通过该模型直接展示最近会议，不再依赖写死的 ListModel。
    Q_PROPERTY(int count READ count NOTIFY recentMeetingsChanged)
    // 区分“尚未收到服务端结果”和“服务端明确返回空列表”，供 QML 显示空状态。
    Q_PROPERTY(bool recentMeetingsLoaded READ recentMeetingsLoaded NOTIFY recentMeetingsChanged)

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

    // 将服务端 recent meetings JSON 原子替换为 QML 可读模型数据。
    bool applyRecentMeeting(const QJsonArray &json);

signals:
    void recentMeetingsChanged();

private:
    struct MeetingItem {
        QString meetingId;
        QString title;
        QString schedule;
        QString participants;
        QString status;
        QString statusColor;
    };

    QVector<MeetingItem> m_recentMeetings;
    bool m_recentMeetingsLoaded = false;
};

#endif // MEETINGCONTROLLER_H
