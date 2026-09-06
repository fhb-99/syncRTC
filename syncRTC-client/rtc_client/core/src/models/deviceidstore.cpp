#include "deviceidstore.h"

#include <QDebug>
#include <QSettings>
#include <QUuid>

namespace {
const QString kDeviceIdKey = QStringLiteral("identity/device_id");
}

QString DeviceIdStore::loadOrCreate(QSettings &settings)
{
    const QString existingId = settings.value(kDeviceIdKey).toString();
    if (!existingId.isEmpty()) {
        return existingId;
    }

    // device_id 只标识当前客户端安装，不是登录凭证。
    const QString deviceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    settings.setValue(kDeviceIdKey, deviceId);
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qWarning() << "Failed to persist device_id:" << settings.fileName();
    }

    return deviceId;
}
