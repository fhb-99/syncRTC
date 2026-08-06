#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "../src/controllers/contacts/contactscontroller.h"

Q_DECLARE_METATYPE(RequestID)
Q_DECLARE_METATYPE(Modules)

namespace {

class GateServerUrlRestore
{
public:
    GateServerUrlRestore() : m_previousUrl(GateServer_URL) {}
    ~GateServerUrlRestore() { GateServer_URL = m_previousUrl; }

private:
    QString m_previousUrl;
};

} // namespace

class ContactsControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RequestID>("RequestID");
        qRegisterMetaType<Modules>("Modules");
    }

    void enteringContactsPageRequestsContacts()
    {
        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:8080");

        ContactsController controller;
        QObject::disconnect(&controller, nullptr, HttpMgr::GetInstance().get(), nullptr);
        QSignalSpy requestSpy(&controller, &ContactsController::signal_contacts_http_request);

        controller.onContactsPageEntered();

        QCOMPARE(requestSpy.count(), 1);
        const QList<QVariant> arguments = requestSpy.takeFirst();
        QCOMPARE(arguments.at(0).toUrl().path(), QStringLiteral("/get_contacts"));
        QCOMPARE(arguments.at(2).value<RequestID>(), RequestID::ID_GET_CONTACTS);
        QCOMPARE(arguments.at(3).value<Modules>(), Modules::CONTACTS_MOD);
    }

    void missingGateServerUrlReportsLoadFailure()
    {
        const GateServerUrlRestore restoreUrl;
        GateServer_URL.clear();

        ContactsController controller;
        QObject::disconnect(&controller, nullptr, HttpMgr::GetInstance().get(), nullptr);
        QSignalSpy failedSpy(&controller, &ContactsController::contactsLoadFailed);
        QSignalSpy requestSpy(&controller, &ContactsController::signal_contacts_http_request);

        controller.requestContacts();

        QCOMPARE(requestSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(failedSpy.takeFirst().at(0).toString().contains(QStringLiteral("GateServer")));
    }

    void contactOperationRequestsAreEmitted()
    {
        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:8080");

        ContactsController controller;
        QObject::disconnect(&controller, nullptr, HttpMgr::GetInstance().get(), nullptr);
        QSignalSpy requestSpy(&controller, &ContactsController::signal_contacts_http_request);

        controller.searchContacts(QStringLiteral(" bob "));
        controller.addContact(7);
        controller.deleteContact(8);

        QCOMPARE(requestSpy.count(), 3);

        QList<QVariant> arguments = requestSpy.takeFirst();
        QCOMPARE(arguments.at(0).toUrl().path(), QStringLiteral("/search_contacts"));
        QCOMPARE(arguments.at(1).toJsonObject().value("keyword").toString(), QStringLiteral("bob"));
        QCOMPARE(arguments.at(2).value<RequestID>(), RequestID::ID_SEARCH_CONTACTS);

        arguments = requestSpy.takeFirst();
        QCOMPARE(arguments.at(0).toUrl().path(), QStringLiteral("/add_contact"));
        QCOMPARE(arguments.at(1).toJsonObject().value("contact_uid").toInt(), 7);
        QCOMPARE(arguments.at(2).value<RequestID>(), RequestID::ID_ADD_CONTACT);

        arguments = requestSpy.takeFirst();
        QCOMPARE(arguments.at(0).toUrl().path(), QStringLiteral("/delete_contact"));
        QCOMPARE(arguments.at(1).toJsonObject().value("contact_uid").toInt(), 8);
        QCOMPARE(arguments.at(2).value<RequestID>(), RequestID::ID_DELETE_CONTACT);
    }

    void contactResponseUpdatesContacts()
    {
        ContactsController controller;
        QSignalSpy loadedSpy(&controller, &ContactsController::contactsLoaded);
        QSignalSpy changedSpy(&controller, &ContactsController::contactsChanged);

        QJsonObject response;
        response["error"] = ErrorCodes::SUCCESS;
        response["count"] = 1;
        response["contacts"] = QJsonArray{
            QJsonObject{
                {"uid", 7},
                {"username", "bob"},
                {"email", "bob@example.com"},
                {"display_name", "Bob"},
                {"alias", "Bobby"},
                {"remark", "project"},
                {"relation_status", 1},
                {"status", "in_meeting"},
            },
        };

        controller.slot_contacts_mod_finish(
            RequestID::ID_GET_CONTACTS,
            QJsonDocument(response).toJson(QJsonDocument::Compact),
            ErrorCodes::SUCCESS);

        QCOMPARE(loadedSpy.count(), 1);
        QCOMPARE(changedSpy.count(), 1);

        const QVariantList contacts = controller.contacts();
        QCOMPARE(contacts.size(), 1);
        const QVariantMap contact = contacts.first().toMap();
        QCOMPARE(contact.value("uid").toInt(), 7);
        QCOMPARE(contact.value("name").toString(), QStringLiteral("Bobby"));
        QCOMPARE(contact.value("status").toString(), QStringLiteral("会议中"));
        QCOMPARE(contact.value("statusValue").toString(), QStringLiteral("in_meeting"));
        QCOMPARE(contact.value("statusColor").toString(), QStringLiteral("#2563eb"));
    }

    void reservedContactOperationsNotifyQml()
    {
        ContactsController controller;
        QSignalSpy searchSpy(&controller, &ContactsController::contactsSearchFinished);
        QSignalSpy addSpy(&controller, &ContactsController::contactAddFinished);
        QSignalSpy deleteSpy(&controller, &ContactsController::contactDeleteFinished);

        QJsonObject success;
        success["error"] = ErrorCodes::SUCCESS;
        success["uid"] = 7;

        controller.slot_contacts_mod_finish(
            RequestID::ID_SEARCH_CONTACTS,
            QJsonDocument(success).toJson(QJsonDocument::Compact),
            ErrorCodes::SUCCESS);
        controller.slot_contacts_mod_finish(
            RequestID::ID_ADD_CONTACT,
            QJsonDocument(success).toJson(QJsonDocument::Compact),
            ErrorCodes::SUCCESS);
        controller.slot_contacts_mod_finish(
            RequestID::ID_DELETE_CONTACT,
            QJsonDocument(success).toJson(QJsonDocument::Compact),
            ErrorCodes::SUCCESS);

        QCOMPARE(searchSpy.count(), 1);
        QCOMPARE(addSpy.count(), 1);
        QCOMPARE(deleteSpy.count(), 1);
        QCOMPARE(addSpy.takeFirst().at(0).toInt(), 7);
        QCOMPARE(deleteSpy.takeFirst().at(0).toInt(), 7);
        QCOMPARE(controller.contactsMessage(), QStringLiteral("联系人删除成功"));
    }

    void reservedContactOperationFailureNotifiesQml()
    {
        ContactsController controller;
        QSignalSpy failedSpy(&controller, &ContactsController::contactsOperationFailed);

        QJsonObject failed;
        failed["error"] = ErrorCodes::ERROR_MYSQL;

        controller.slot_contacts_mod_finish(
            RequestID::ID_ADD_CONTACT,
            QJsonDocument(failed).toJson(QJsonDocument::Compact),
            ErrorCodes::SUCCESS);

        QCOMPARE(failedSpy.count(), 1);
        QCOMPARE(failedSpy.takeFirst().at(0).toInt(), RequestID::ID_ADD_CONTACT);
    }

    void invalidContactOperationRequestReportsFailure()
    {
        const GateServerUrlRestore restoreUrl;
        GateServer_URL = QStringLiteral("http://127.0.0.1:8080");

        ContactsController controller;
        QObject::disconnect(&controller, nullptr, HttpMgr::GetInstance().get(), nullptr);
        QSignalSpy failedSpy(&controller, &ContactsController::contactsOperationFailed);
        QSignalSpy requestSpy(&controller, &ContactsController::signal_contacts_http_request);

        controller.searchContacts(QStringLiteral("   "));
        controller.addContact(0);
        controller.deleteContact(-1);

        QCOMPARE(requestSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 3);
        QCOMPARE(failedSpy.takeFirst().at(0).toInt(), RequestID::ID_SEARCH_CONTACTS);
        QCOMPARE(failedSpy.takeFirst().at(0).toInt(), RequestID::ID_ADD_CONTACT);
        QCOMPARE(failedSpy.takeFirst().at(0).toInt(), RequestID::ID_DELETE_CONTACT);
    }
};

QTEST_GUILESS_MAIN(ContactsControllerTest)

#include "contactscontroller_test.moc"
