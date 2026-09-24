#ifndef RTCTRANSPORTWORKER_H
#define RTCTRANSPORTWORKER_H

#include <QByteArray>
#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

#include <memory>
#include <mutex>

class MediaSession;

// 该类是客户端发送侧唯一接触 PeerConnection 与 rtc::Track 的执行上下文。
// 音频/视频编码线程只提交已经完成 RTP 封装的数据，不能直接访问 Track。
class RtcTransportWorker : public QObject
{
    Q_OBJECT
public:
    explicit RtcTransportWorker(QObject *parent = nullptr);
    ~RtcTransportWorker() override;

    // 以下两个入队函数可由音频、视频编码线程直接调用，内部只进行有界入队和唤醒。
    void enqueueVideoRtpPackets(const QVector<QByteArray> &packets);
    void enqueueAudioRtpPacket(const QByteArray &packet);

public slots:
    // 以下命令仅由 RTC 传输线程执行，确保 PeerConnection、Track 与发送操作串行。
    void startMediaSession(const QString &meetingId);
    void stopMediaSession();
    void setRemoteDescription(const QString &sdp, const QString &type);
    void setRemoteOffer(const QString &sdp);
    void addRemoteCandidate(const QString &candidate, const QString &mid);
    void setVideoSendingEnabled(bool enabled);
    void setAudioSendingEnabled(bool enabled);

private slots:
    void processPendingRtp();

signals:
    void localOfferReady(const QString &meetingId, const QString &sdp);
    void localAnswerReady(const QString &meetingId, const QString &sdp);
    void localCandidateReady(const QString &meetingId, const QString &candidate,
                             const QString &mid);
    void remoteVideoEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                      quint32 rtpTimestamp);
    void remoteAudioEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                      quint32 rtpTimestamp);

private:
    // 视频按完整 Access Unit 入队，丢帧时不会只丢掉 FU-A 的其中一个分片。
    static constexpr int MaxQueuedVideoFrames = 2;
    static constexpr int MaxQueuedAudioPackets = 3;

    MediaSession *m_mediaSession = nullptr;
    bool m_videoSendingEnabled = false;
    bool m_audioSendingEnabled = false;
    // 同一会议内保持同一个 PeerConnection，摄像头重开时只恢复视频发送。
    bool m_mediaSessionStarted = false;

    std::mutex m_queueMutex;
    QQueue<QVector<QByteArray>> m_videoRtpFrames;
    QQueue<QByteArray> m_audioRtpPackets;
    bool m_sendScheduled = false;
};

#endif // RTCTRANSPORTWORKER_H
