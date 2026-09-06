#ifndef DEVICEIDSTORE_H
#define DEVICEIDSTORE_H

#include <QString>

class QSettings;

class DeviceIdStore
{
public:
    static QString loadOrCreate(QSettings &settings);
};

#endif // DEVICEIDSTORE_H
