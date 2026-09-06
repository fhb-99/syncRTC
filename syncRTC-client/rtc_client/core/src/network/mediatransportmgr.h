#ifndef MEDIATRANSPORTMGR_H
#define MEDIATRANSPORTMGR_H

#include <QObject>
#include <QByteArray>

#include <memory>

#include "../models/Singleton.h"

namespace rtc {
class Track;
}

class MediaTransportMgr : public QObject, public Singleton<MediaTransportMgr>,
                          public std::enable_shared_from_this<MediaTransportMgr>
{
    Q_OBJECT
    friend class Singleton<MediaTransportMgr>;
public:
    ~MediaTransportMgr() = default;

    void setVideoTrack(const std::shared_ptr<rtc::Track> &track);
    void setAudioTrack(const std::shared_ptr<rtc::Track> &track);
    void clearVideoTrack();
    void clearAudioTrack();
    void sendVideoRtp(const QByteArray &packet);
    void sendAudioRtp(const QByteArray &packet);

private:
    explicit MediaTransportMgr(QObject *parent = nullptr);

    std::weak_ptr<rtc::Track> m_videoTrack;
    std::weak_ptr<rtc::Track> m_audioTrack;
};

#endif // MEDIATRANSPORTMGR_H
