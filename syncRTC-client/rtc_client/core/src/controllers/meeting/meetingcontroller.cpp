#include "meetingcontroller.h"

#include <QJsonObject>

MeetingController::MeetingController(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MeetingController::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_recentMeetings.size();
}

QVariant MeetingController::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_recentMeetings.size()) {
        return {};
    }

    const MeetingItem &item = m_recentMeetings.at(index.row());
    switch (role) {
    case MeetingIdRole:
        return item.meetingId;
    case TitleRole:
        return item.title;
    case ScheduleRole:
        return item.schedule;
    case ParticipantsRole:
        return item.participants;
    case StatusRole:
        return item.status;
    case StatusColorRole:
        return item.statusColor;
    default:
        return {};
    }
}

QHash<int, QByteArray> MeetingController::roleNames() const
{
    return {
        {MeetingIdRole, "meetingId"},
        {TitleRole, "title"},
        {ScheduleRole, "schedule"},
        {ParticipantsRole, "participants"},
        {StatusRole, "status"},
        {StatusColorRole, "statusColor"},
    };
}

int MeetingController::count() const
{
    return m_recentMeetings.size();
}

bool MeetingController::recentMeetingsLoaded() const
{
    return m_recentMeetingsLoaded;
}

bool MeetingController::applyRecentMeeting(const QJsonArray &json)
{
    QVector<MeetingItem> updatedMeetings;
    updatedMeetings.reserve(json.size());

    for (const QJsonValue &value : json) {
        if (!value.isObject()) {
            return false;
        }

        const QJsonObject object = value.toObject();
        MeetingItem item;
        item.meetingId = object.value("meeting_id").toVariant().toString().trimmed();
        if (item.meetingId.isEmpty()) {
            item.meetingId = object.value("meeting_code").toString().trimmed();
        }
        item.title = object.value("title").toString().trimmed();
        if (item.meetingId.isEmpty() || item.title.isEmpty()) {
            return false;
        }

        item.schedule = object.value("schedule").toString().trimmed();
        if (item.schedule.isEmpty()) {
            item.schedule = object.value("scheduled_at").toString().trimmed();
        }
        if (item.schedule.isEmpty()) {
            item.schedule = object.value("start_time").toString().trimmed();
        }
        if (item.schedule.isEmpty()) {
            item.schedule = QStringLiteral("时间待定");
        }

        const QJsonValue participantValue = object.value("participant_count");
        const int participantCount = participantValue.isString()
            ? participantValue.toString().toInt()
            : participantValue.toInt();
        item.participants = QStringLiteral("%1 人").arg(qMax(0, participantCount));

        const QJsonValue statusValue = object.value("status");
        const QString statusCode = statusValue.isString()
            ? statusValue.toString().toLower()
            : QString::number(statusValue.toInt());
        if (statusCode == "in_progress" || statusCode == "1") {
            item.status = QStringLiteral("进行中");
            item.statusColor = QStringLiteral("#16a34a");
        } else if (statusCode == "scheduled" || statusCode == "0") {
            item.status = QStringLiteral("已预约");
            item.statusColor = QStringLiteral("#2563eb");
        } else if (statusCode == "cancelled" || statusCode == "3") {
            item.status = QStringLiteral("已取消");
            item.statusColor = QStringLiteral("#ef4444");
        } else {
            item.status = QStringLiteral("已结束");
            item.statusColor = QStringLiteral("#64748b");
        }

        updatedMeetings.append(item);
    }

    beginResetModel();
    m_recentMeetings = std::move(updatedMeetings);
    // applyRecentMeeting 仅在完整 JSON 校验通过后调用到这里，表示已收到有效服务端结果。
    m_recentMeetingsLoaded = true;
    endResetModel();
    emit recentMeetingsChanged();
    return true;
}
