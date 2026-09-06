#ifndef MEDIASESSION_H
#define MEDIASESSION_H

#include <QByteArray>
#include <QObject>
#include <QString>

#include <memory>
#include <mutex>
#include <vector>

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

    // 创建客户端到MediaServer的PeerConnection和音视频发送Track，并生成本地offer。
    void startMediaSession(const QString &meetingId);
    void stopMediaSession();
    void setRemoteDescription(const QString &sdp, const QString &type);
    void setRemoteOffer(const QString &sdp);
    void addRemoteCandidate(const QString &candidate, const QString &mid);
    std::shared_ptr<rtc::Track> videoTrack() const;
    std::shared_ptr<rtc::Track> audioTrack() const;

signals:
    // offer/candidate 仍走现有 RealtimeServer 控制链路，不由传输类直接发送。
    void localOfferReady(const QString &meetingId, const QString &sdp);
    void localAnswerReady(const QString &meetingId, const QString &sdp);
    void localCandidateReady(const QString &meetingId, const QString &candidate,
                             const QString &mid);
    // 这里交出的仍是编码数据：视频为 H.264 Annex-B 帧，音频为单个 Opus 帧。
    // 解码、音画同步和播放由后续 receive 目录中的接收链路负责。
    void remoteVideoEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                      quint32 rtpTimestamp);
    void remoteAudioEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                      quint32 rtpTimestamp);

private:
    QString m_meetingId;
    std::shared_ptr<rtc::PeerConnection> m_peerConnection;
    std::shared_ptr<rtc::Track> m_videoTrack;
    std::shared_ptr<rtc::Track> m_audioTrack;
    std::vector<std::shared_ptr<rtc::Track>> m_remoteTracks;
    std::mutex m_remoteTracksMutex;
};

#endif // MEDIASESSION_H
