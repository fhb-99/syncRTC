#include "contactscontroller.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QVariantMap>

namespace {

QString statusColor(const QString &status)
{
    if (status == "in_meeting") {
        return QStringLiteral("#2563eb");
    }
    if (status == "online") {
        return QStringLiteral("#16a34a");
    }
    return QStringLiteral("#94a3b8");
}

QString displayStatus(const QString &status)
{
    if (status == "in_meeting") {
        return QStringLiteral("会议中");
    }
    if (status == "online") {
        return QStringLiteral("在线");
    }
    return QStringLiteral("离线");
}

} // namespace

ContactsController::ContactsController(CurrentUserState *currentUser, QObject *parent)
    : QObject(parent),
      m_currentUser(currentUser)
{
    initHttpHandlers();
}

void ContactsController::initHttpHandlers()
{
    m_handlers.insert(RequestID::ID_GET_CONTACTS, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            m_contactsError = QStringLiteral("联系人加载失败，错误码：%1").arg(error);
            emit contactsErrorChanged();
            emit contactsLoadFailed(m_contactsError);
            return;
        }

        const QJsonValue contactsValue = json.value("contacts");
        if (!contactsValue.isArray()) {
            m_contactsError = QStringLiteral("联系人数据格式异常");
            emit contactsErrorChanged();
            emit contactsLoadFailed(m_contactsError);
            return;
        }

        QVariantList contacts;
        const QJsonArray contactsJson = contactsValue.toArray();
        for (const QJsonValue &value : contactsJson) {
            if (!value.isObject()) {
                continue;
            }

            const QJsonObject itemJson = value.toObject();
            const QString alias = itemJson.value("alias").toString().trimmed();
            const QString displayName = itemJson.value("display_name").toString().trimmed();
            const QString username = itemJson.value("username").toString().trimmed();
            const QString status = itemJson.value("status").toString("offline");

            QVariantMap item;
            item["uid"] = itemJson.value("uid").toInt();
            item["username"] = username;
            item["email"] = itemJson.value("email").toString();
            item["displayName"] = displayName;
            item["alias"] = alias;
            item["remark"] = itemJson.value("remark").toString();
            item["relationStatus"] = itemJson.value("relation_status").toInt();
            item["status"] = displayStatus(status);
            item["statusValue"] = status;
            item["statusColor"] = statusColor(status);
            item["name"] = !alias.isEmpty() ? alias : (!displayName.isEmpty() ? displayName : username);
            contacts.append(item);
        }

        m_contacts = contacts;
        emit contactsChanged();
        emit contactsLoaded(m_contacts);
    });

    m_handlers.insert(RequestID::ID_SEARCH_CONTACTS, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            m_contactsError = QStringLiteral("联系人搜索失败，错误码：%1").arg(error);
            emit contactsErrorChanged();
            emit contactsOperationFailed(RequestID::ID_SEARCH_CONTACTS, m_contactsError);
            return;
        }

        // 预留搜索结果处理入口：后续服务端返回 contacts 数组后，在这里转换为 QML 可展示列表。
        m_searchResults.clear();
        emit searchResultsChanged();
        emit contactsSearchFinished(m_searchResults);
    });

    m_handlers.insert(RequestID::ID_ADD_CONTACT, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            m_contactsError = QStringLiteral("添加联系人失败，错误码：%1").arg(error);
            emit contactsErrorChanged();
            emit contactsOperationFailed(RequestID::ID_ADD_CONTACT, m_contactsError);
            return;
        }

        // 单向添加：这里只通知 QML 操作完成，列表刷新由后续请求或页面刷新触发。
        const int uid = json.value("uid").toInt(json.value("contact_uid").toInt());
        m_contactsMessage = QStringLiteral("联系人添加成功");
        emit contactsMessageChanged();
        emit contactAddFinished(uid);
    });

    m_handlers.insert(RequestID::ID_DELETE_CONTACT, [this](const QJsonObject &json) {
        const int error = json.value("error").toInt(ErrorCodes::ERROR_JSON);
        if (error != ErrorCodes::SUCCESS) {
            m_contactsError = QStringLiteral("删除联系人失败，错误码：%1").arg(error);
            emit contactsErrorChanged();
            emit contactsOperationFailed(RequestID::ID_DELETE_CONTACT, m_contactsError);
            return;
        }

        // 单向删除：这里只通知 QML 操作完成，是否移除本地列表等 UI 策略后续再接。
        const int uid = json.value("uid").toInt(json.value("contact_uid").toInt());
        m_contactsMessage = QStringLiteral("联系人删除成功");
        emit contactsMessageChanged();
        emit contactDeleteFinished(uid);
    });
}

void ContactsController::onContactsPageEntered()
{
    emit contactsLoadRequested();
}

void ContactsController::slot_contacts_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error)
{
    if (m_contactsLoading) {
        m_contactsLoading = false;
        emit contactsLoadingChanged();
    }

    if (error != ErrorCodes::SUCCESS) {
        m_contactsError = QStringLiteral("网络异常");
        emit contactsErrorChanged();
        if (reqID == RequestID::ID_GET_CONTACTS) {
            emit contactsLoadFailed(m_contactsError);
        } else {
            emit contactsOperationFailed(reqID, m_contactsError);
        }
        qDebug() << "Contacts HTTP network error:" << error;
        return;
    }

    const QJsonDocument jsonDoc = QJsonDocument::fromJson(res);
    if (!jsonDoc.isObject()) {
        m_contactsError = QStringLiteral("联系人响应格式异常");
        emit contactsErrorChanged();
        if (reqID == RequestID::ID_GET_CONTACTS) {
            emit contactsLoadFailed(m_contactsError);
        } else {
            emit contactsOperationFailed(reqID, m_contactsError);
        }
        qDebug() << "Contacts HTTP JSON parse error";
        return;
    }

    if (!m_handlers.contains(reqID)) {
        qDebug() << "No contacts handler for request id:" << reqID;
        return;
    }

    m_handlers[reqID](jsonDoc.object());
}
