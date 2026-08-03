#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest>

#include "../src/controllers/contacts/contactscontroller.h"

class ContactsControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void enteringContactsPageRequestsContacts()
    {
        ContactsController controller;
        QSignalSpy loadRequestedSpy(&controller, &ContactsController::contactsLoadRequested);

        controller.onContactsPageEntered();

        QCOMPARE(loadRequestedSpy.count(), 1);
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
};

QTEST_GUILESS_MAIN(ContactsControllerTest)

#include "contactscontroller_test.moc"
