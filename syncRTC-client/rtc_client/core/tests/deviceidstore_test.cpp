#include <QSettings>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

#include "../src/models/deviceidstore.h"

class DeviceIdStoreTest : public QObject
{
    Q_OBJECT

private slots:
    void createsAndPersistsInstallationId()
    {
        QTemporaryDir temporaryDirectory;
        QVERIFY(temporaryDirectory.isValid());

        const QString settingsPath = temporaryDirectory.filePath("client.ini");
        QSettings firstSettings(settingsPath, QSettings::IniFormat);
        const QString createdId = DeviceIdStore::loadOrCreate(firstSettings);

        QVERIFY(!createdId.isEmpty());
        QCOMPARE(QUuid(createdId).toString(QUuid::WithoutBraces), createdId);

        QSettings secondSettings(settingsPath, QSettings::IniFormat);
        QCOMPARE(DeviceIdStore::loadOrCreate(secondSettings), createdId);
    }
};

QTEST_GUILESS_MAIN(DeviceIdStoreTest)

#include "deviceidstore_test.moc"
