#ifndef CONTACTSCONTROLLER_H
#define CONTACTSCONTROLLER_H

#include <QObject>
#include <QJsonObject>
#include <QMap>
#include <QUrl>
#include <QVariantList>

#include <functional>

#include "../../models/currentuserstate.h"
#include "../../network/httpmgr.h"

class ContactsController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool contactsLoading READ contactsLoading NOTIFY contactsLoadingChanged)
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QString contactsError READ contactsError NOTIFY contactsErrorChanged)
    Q_PROPERTY(QString contactsMessage READ contactsMessage NOTIFY contactsMessageChanged)

public:
    explicit ContactsController(CurrentUserState *currentUser = nullptr, QObject *parent = nullptr);

    // 通讯录页面切入时由 QML 调用；控制器负责发起请求和处理回包。
    Q_INVOKABLE void onContactsPageEntered();
    Q_INVOKABLE void requestContacts();
    Q_INVOKABLE void searchContacts(const QString &keyword);
    Q_INVOKABLE void addContact(int uid);
    Q_INVOKABLE void deleteContact(int uid);

    bool contactsLoading() const { return m_contactsLoading; }
    QVariantList contacts() const { return m_contacts; }
    QVariantList searchResults() const { return m_searchResults; }
    QString contactsError() const { return m_contactsError; }
    QString contactsMessage() const { return m_contactsMessage; }

signals:
    void signal_contacts_http_request(QUrl url, QJsonObject jsonObj, RequestID reqID, Modules module);
    void contactsLoaded(const QVariantList &contacts);
    void contactsLoadFailed(const QString &reason);
    void contactsSearchFinished(const QVariantList &contacts);
    void contactAddFinished(int uid);
    void contactDeleteFinished(int uid);
    void contactsOperationFailed(int reqID, const QString &reason);
    void contactsLoadingChanged();
    void contactsChanged();
    void searchResultsChanged();
    void contactsErrorChanged();
    void contactsMessageChanged();

public slots:
    void slot_contacts_mod_finish(RequestID reqID, QByteArray res, ErrorCodes error);

private:
    void initHttpHandlers();

    CurrentUserState *m_currentUser = nullptr;
    QMap<RequestID, std::function<void(const QJsonObject&)>> m_handlers;
    bool m_contactsLoading = false;
    QVariantList m_contacts;
    QVariantList m_searchResults;
    QString m_contactsError;
    QString m_contactsMessage;
};

#endif // CONTACTSCONTROLLER_H
