#ifndef MEDIATRANSPORTMGR_H
#define MEDIATRANSPORTMGR_H

#include <QObject>
#include <QByteArray>
#include <QString>

#include <memory>

#include "../models/Singleton.h"

class MediaTransportMgr : public QObject, public Singleton<MediaTransportMgr>,
                          public std::enable_shared_from_this<MediaTransportMgr>
{
    Q_OBJECT
    friend class Singleton<MediaTransportMgr>;
public:
    ~MediaTransportMgr() = default;

    void startMediaSession(const QString &meetingId);
    void stopMediaSession();
    void sendVideoRtp(const QByteArray &packet);
    void sendAudioRtp(const QByteArray &packet);

private:
    explicit MediaTransportMgr(QObject *parent = nullptr);
};

#endif // MEDIATRANSPORTMGR_H
