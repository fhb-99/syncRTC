#ifndef MEDIASESSION_H
#define MEDIASESSION_H

#include <QObject>
#include <QString>

#include <memory>

namespace rtc {
class PeerConnection;
class Track;
}

class MediaSession : public QObject
{
    Q_OBJECT
public:
    explicit MediaSession(QObject *parent = nullptr);
    ~MediaSession() override;

    // 预留：后续在这里创建 PeerConnection、Track，并生成 offer。
    void startMediaSession(const QString &meetingId);
    void stopMediaSession();
    void setRemoteDescription(const QString &sdp, const QString &type);
    void addRemoteCandidate(const QString &candidate, const QString &mid);
    std::shared_ptr<rtc::Track> videoTrack() const;

signals:
    // offer/candidate 仍走现有 RealtimeServer 控制链路，不由传输类直接发送。
    void localOfferReady(const QString &meetingId, const QString &sdp);
    void localCandidateReady(const QString &meetingId, const QString &candidate,
                             const QString &mid);

private:
    QString m_meetingId;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::Track> m_videoTrack;
};

#endif // MEDIASESSION_H
