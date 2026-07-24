#include "meetingcontroller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include "../../network/tcpmgr.h"

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

QVariantList MeetingController::historyMeetings() const
{
    return m_historyMeetings;
}

void MeetingController::requestCreateMeeting(const QString &title, const QString &scheduledAt,
                                             const QString &password)
{
    QJsonObject request;
    request["title"] = title;
    // 空值表示立即开始；非空值由 QML 时间选择器生成
    request["scheduled_at"] = scheduledAt;
    // 空值表示默认不加密，服务端应只保存密码哈希
    request["password"] = password;

    TcpMgr::GetInstance()->slot_send_data(
        ID_CREATE_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MeetingController::requestHistoryMeetings()
{
    QJsonObject request;

    TcpMgr::GetInstance()->slot_send_data(
        ID_PAST_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

bool MeetingController::copyMeetingCode(const QString &meetingCode)
{
    const QString code = meetingCode.trimmed();
    if (code.isEmpty()) {
        return false;
    }

    QGuiApplication::clipboard()->setText(code);
    return true;
}

bool MeetingController::parseMeetingItem(const QJsonObject &object, MeetingItem *item)
{
    if (item == nullptr) {
        return false;
    }

    MeetingItem parsed;
    parsed.meetingId = object.value("meeting_id").toVariant().toString().trimmed();
    if (parsed.meetingId.isEmpty()) {
        parsed.meetingId = object.value("meeting_code").toString().trimmed();
    }
    parsed.title = object.value("title").toString().trimmed();
    if (parsed.meetingId.isEmpty() || parsed.title.isEmpty()) {
        return false;
    }

    parsed.schedule = object.value("schedule").toString().trimmed();
    if (parsed.schedule.isEmpty()) {
        parsed.schedule = object.value("scheduled_at").toString().trimmed();
    }
    if (parsed.schedule.isEmpty()) {
        parsed.schedule = object.value("start_time").toString().trimmed();
    }
    if (parsed.schedule.isEmpty()) {
        parsed.schedule = QStringLiteral("时间待定");
    }

    const QJsonValue participantValue = object.value("participant_count");
    const int participantCount = participantValue.isString()
        ? participantValue.toString().toInt()
        : participantValue.toInt();
    parsed.participants = QStringLiteral("%1 人").arg(qMax(0, participantCount));

    const QJsonValue statusValue = object.value("status");
    const QString statusCode = statusValue.isString()
        ? statusValue.toString().toLower()
        : QString::number(statusValue.toInt());
    if (statusCode == "in_progress" || statusCode == "1") {
        parsed.status = QStringLiteral("进行中");
        parsed.statusColor = QStringLiteral("#16a34a");
    } else if (statusCode == "scheduled" || statusCode == "0") {
        parsed.status = QStringLiteral("已预约");
        parsed.statusColor = QStringLiteral("#2563eb");
    } else if (statusCode == "cancelled" || statusCode == "3") {
        parsed.status = QStringLiteral("已取消");
        parsed.statusColor = QStringLiteral("#ef4444");
    } else {
        parsed.status = QStringLiteral("已结束");
        parsed.statusColor = QStringLiteral("#64748b");
    }

    *item = std::move(parsed);
    return true;
}

bool MeetingController::applyRecentMeeting(const QJsonArray &json)
{
    QVector<MeetingItem> updatedMeetings;
    updatedMeetings.reserve(json.size());

    for (const QJsonValue &value : json) {
        if (!value.isObject()) {
            return false;
        }

        MeetingItem item;
        if (!parseMeetingItem(value.toObject(), &item)) {
            return false;
        }
        updatedMeetings.append(std::move(item));
    }

    beginResetModel();
    m_recentMeetings = std::move(updatedMeetings);
    // applyRecentMeeting 仅在完整 JSON 校验通过后调用到这里，表示已收到有效服务端结果
    m_recentMeetingsLoaded = true;
    endResetModel();
    emit recentMeetingsChanged();
    return true;
}

bool MeetingController::applyHistoryMeetings(const QJsonArray &json)
{
    QVariantList updatedMeetings;
    updatedMeetings.reserve(json.size());

    for (const QJsonValue &value : json) {
        if (!value.isObject()) {
            return false;
        }

        MeetingItem item;
        if (!parseMeetingItem(value.toObject(), &item)) {
            return false;
        }

        // QVariantMap 可直接作为 QML ListView 的 modelData 读取。
        updatedMeetings.append(QVariantMap{
            {"meetingId", item.meetingId},
            {"title", item.title},
            {"schedule", item.schedule},
            {"participants", item.participants},
            {"status", item.status},
            {"statusColor", item.statusColor},
        });
    }

    m_historyMeetings = std::move(updatedMeetings);
    emit historyMeetingsChanged();
    return true;
}
