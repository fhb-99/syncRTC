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
    // 将内部会议实体按角色暴露给首页 ListView，避免 QML 依赖 JSON 字段名。
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

void MeetingController::requestJoinMeeting(const QString &meetingCode)
{
    const QString code = meetingCode.trimmed();
    if (code.isEmpty()) {
        emit joinMeetingFailed(ErrorCodes::ERROR_JSON);
        return;
    }

    QJsonObject request;
    // 会议号是用户申请入会的唯一输入，后续服务端负责校验会议状态与入会额度。
    request["meeting_code"] = code;
    // 当前入会成功回包只包含 error，因此先保存本次已发送的会议号用于成功确认。
    m_pendingJoinMeetingCode = code;

    TcpMgr::GetInstance()->signal_send_data(
        ID_JOIN_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MeetingController::requestStartMeeting(const QString &meetingId)
{
    const QString id = meetingId.trimmed();
    if (id.isEmpty()) {
        emit startMeetingFailed(ErrorCodes::ERROR_JSON);
        return;
    }

    QJsonObject request;
    request["meeting_id"] = id;
    m_pendingStartMeetingId = id;

    TcpMgr::GetInstance()->signal_send_data(
        ID_START_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MeetingController::requestLeaveMeeting(const QString &meetingId)
{
    const QString id = meetingId.trimmed();
    if (id.isEmpty()) {
        emit leaveMeetingFailed(ErrorCodes::ERROR_JSON);
        return;
    }

    QJsonObject request;
    request["meeting_id"] = id;
    m_pendingLeaveMeetingId = id;

    TcpMgr::GetInstance()->signal_send_data(
        ID_LEAVE_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MeetingController::requestEndMeeting(const QString &meetingId)
{
    const QString id = meetingId.trimmed();
    if (id.isEmpty()) {
        emit endMeetingFailed(ErrorCodes::ERROR_JSON);
        return;
    }

    QJsonObject request;
    request["meeting_id"] = id;
    m_pendingEndMeetingId = id;

    TcpMgr::GetInstance()->signal_send_data(
        ID_END_MEETING_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
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
    // 服务端历史接口使用 meeting_code，部分接口仍使用 meeting_id，客户端兼容两种字段。
    parsed.meetingId = object.value("meeting_id").toVariant().toString().trimmed();
    if (parsed.meetingId.isEmpty()) {
        parsed.meetingId = object.value("meeting_code").toString().trimmed();
    }
    // 会议号和主题是界面展示所必需的数据，缺失时拒绝整次更新，保留旧数据。
    parsed.title = object.value("title").toString().trimmed();
    if (parsed.meetingId.isEmpty() || parsed.title.isEmpty()) {
        return false;
    }

    // 不同回包阶段的时间字段不同，按统一优先级转换为界面展示时间。
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

    // participant_count 兼容数字和字符串，统一转换并限制为非负人数。
    const QJsonValue participantValue = object.value("participant_count");
    const int participantCount = participantValue.isString()
        ? participantValue.toString().toInt()
        : participantValue.toInt();
    parsed.participants = QStringLiteral("%1 人").arg(qMax(0, participantCount));

    // 将协议状态码映射为中文文案和页面状态色，QML 不再承载业务判断。
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

    // 所有记录校验通过后一次性替换首页模型，避免 QML 看到半更新的数据。
    beginResetModel();
    m_recentMeetings = std::move(updatedMeetings);
    // applyRecentMeeting 仅在完整 JSON 校验通过后调用到这里，表示已收到有效服务端结果
    m_recentMeetingsLoaded = true;
    endResetModel();
    // 通知首页的空状态、会议数量和列表委托同步刷新。
    emit recentMeetingsChanged();
    return true;
}

bool MeetingController::applyHistoryMeetings(const QJsonArray &json)
{
    QVariantList updatedMeetings;
    updatedMeetings.reserve(json.size());

    // 服务端已按距离当前时间由近到远排序，客户端只转换字段，不再自行重新排序。
    for (const QJsonValue &value : json) {
        if (!value.isObject()) {
            return false;
        }

        const QJsonObject object = value.toObject();
        // 历史会议使用 HistoryMeetingInfo 协议，字段与首页最近会议不同，不能复用 parseMeetingItem。
        const QString meetingCode = object.value("meeting_code").toString().trimmed();
        const QString title = object.value("title").toString().trimmed();
        if (meetingCode.isEmpty() || title.isEmpty()) {
            return false;
        }

        // 创建者头像允许为空，QML 会在没有头像地址时展示默认头像。
        const QString creatorName = object.value("creator_display_name").toString().trimmed();
        const QString creatorAvatarUrl = object.value("creator_avatar_url").toString().trimmed();
        // 历史会议应带有完整起止时间；缺失时保留明确的占位文案，避免界面出现空白。
        const QString startedAt = object.value("started_at").toString().trimmed();
        const QString endedAt = object.value("ended_at").toString().trimmed();

        // QVariantMap 可直接作为 QML ListView 的 modelData 读取。
        updatedMeetings.append(QVariantMap{
            {"meetingCode", meetingCode},
            {"title", title},
            {"creatorName", creatorName.isEmpty() ? QStringLiteral("未知创建者") : creatorName},
            {"creatorAvatarUrl", creatorAvatarUrl},
            {"startedAt", startedAt.isEmpty() ? QStringLiteral("开始时间待定") : startedAt},
            {"endedAt", endedAt.isEmpty() ? QStringLiteral("结束时间待定") : endedAt},
        });
    }

    // 历史列表独立于最近会议模型，防止切换历史页后覆盖首页数据。
    m_historyMeetings = std::move(updatedMeetings);
    // historyMeetings 属性变化后，历史页的 ListView 会重新绑定新列表。
    emit historyMeetingsChanged();
    return true;
}

bool MeetingController::applyJoinMeetingResponse(const QJsonObject &json)
{
    const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        m_pendingJoinMeetingCode.clear();
        emit joinMeetingFailed(error);
        return false;
    }

    if (m_pendingJoinMeetingCode.isEmpty()) {
        // 没有对应请求的成功回包不能驱动 QML 跳转，避免旧包或异常包误入会议。
        emit joinMeetingFailed(ErrorCodes::ERROR_JSON);
        return false;
    }

    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    const QString meetingCode = json.value("meeting_code").toString().trimmed();
    const QString status = json.value("status").toString().trimmed().toLower();
    const QString role = json.value("role").toString().trimmed().toLower();
    const QJsonValue membersValue = json.value("members");
    if (meetingId.isEmpty() || meetingCode != m_pendingJoinMeetingCode ||
        (status != QStringLiteral("scheduled") && status != QStringLiteral("in_progress") &&
         status != QStringLiteral("ended")) ||
        (role != QStringLiteral("host") && role != QStringLiteral("participant")) ||
        !membersValue.isArray()) {
        // 入会成功回包必须携带会议身份和状态，避免 QML 根据不完整数据进入错误界面。
        m_pendingJoinMeetingCode.clear();
        qWarning() << "Join meeting Failed, error is: " << error;
        emit joinMeetingFailed(ErrorCodes::ERROR_JSON);
        return false;
    }

    m_pendingJoinMeetingCode.clear();
    emit joinMeetingSucceeded(meetingCode, meetingId, status, role,
                              membersValue.toArray().toVariantList());
    return true;
}

bool MeetingController::applyStartMeetingResponse(const QJsonObject &json)
{
    const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        m_pendingStartMeetingId.clear();
        emit startMeetingFailed(error);
        return false;
    }

    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    const QString status = json.value("status").toString().trimmed().toLower();
    if (m_pendingStartMeetingId.isEmpty() || meetingId != m_pendingStartMeetingId ||
        status != QStringLiteral("in_progress")) {
        m_pendingStartMeetingId.clear();
        emit startMeetingFailed(ErrorCodes::ERROR_JSON);
        return false;
    }

    m_pendingStartMeetingId.clear();
    emit startMeetingSucceeded(meetingId);
    return true;
}

bool MeetingController::applyMeetingStarted(const QJsonObject &json)
{
    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    const QString status = json.value("status").toString().trimmed().toLower();
    if (meetingId.isEmpty() || status != QStringLiteral("in_progress")) {
        return false;
    }

    emit meetingStarted(meetingId);
    return true;
}

bool MeetingController::applyLeaveMeetingResponse(const QJsonObject &json)
{
    const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        m_pendingLeaveMeetingId.clear();
        emit leaveMeetingFailed(error);
        return false;
    }

    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    if (m_pendingLeaveMeetingId.isEmpty() || meetingId != m_pendingLeaveMeetingId) {
        m_pendingLeaveMeetingId.clear();
        emit leaveMeetingFailed(ErrorCodes::ERROR_JSON);
        return false;
    }

    m_pendingLeaveMeetingId.clear();
    emit leaveMeetingSucceeded(meetingId);
    return true;
}

bool MeetingController::applyEndMeetingResponse(const QJsonObject &json)
{
    const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
    if (error != ErrorCodes::SUCCESS) {
        m_pendingEndMeetingId.clear();
        emit endMeetingFailed(error);
        return false;
    }

    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    const QString status = json.value("status").toString().trimmed().toLower();
    if (m_pendingEndMeetingId.isEmpty() || meetingId != m_pendingEndMeetingId ||
        status != QStringLiteral("ended")) {
        m_pendingEndMeetingId.clear();
        emit endMeetingFailed(ErrorCodes::ERROR_JSON);
        return false;
    }

    m_pendingEndMeetingId.clear();
    emit endMeetingSucceeded(meetingId);
    return true;
}

bool MeetingController::applyMeetingEnded(const QJsonObject &json)
{
    const QString meetingId = json.value("meeting_id").toVariant().toString().trimmed();
    const QString status = json.value("status").toString().trimmed().toLower();
    if (meetingId.isEmpty() || status != QStringLiteral("ended")) {
        return false;
    }

    emit meetingEnded(meetingId);
    return true;
}
