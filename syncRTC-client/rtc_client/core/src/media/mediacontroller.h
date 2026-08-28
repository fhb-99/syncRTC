#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QObject>
#include <QString>
#include <QVideoFrame>

#include <memory>
#include <unordered_map>

class QJsonObject;
class QVideoSink;
class MediaDeviceCapture;
class RemoteMediaReceiver;
class RemoteVideoRenderer;
class MediaSession;
class MediaStreamProcessor;

class MediaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool cameraEnabled READ cameraEnabled NOTIFY cameraEnabledChanged)
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled NOTIFY microphoneEnabledChanged)
    Q_PROPERTY(bool localVideoAvailable READ localVideoAvailable
               NOTIFY localVideoAvailableChanged)
public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    bool cameraEnabled() const { return m_cameraEnabled; }
    bool microphoneEnabled() const { return m_microphoneEnabled; }
    bool localVideoAvailable() const;

    Q_INVOKABLE void requestOpenCamera(const QString &meetingId);
    Q_INVOKABLE void requestCloseCamera(const QString &meetingId);
    Q_INVOKABLE void requestOpenMicrophone();
    Q_INVOKABLE void requestCloseMicrophone();
    Q_INVOKABLE void requestStopAll();

    Q_INVOKABLE bool applyMediaAnswer(const QJsonObject &json);
    Q_INVOKABLE bool applyMediaOffer(const QJsonObject &json);
    Q_INVOKABLE bool applyMediaCandidate(const QJsonObject &json);
    // VideoOutput.videoSink 是只读对象，QML 创建成员画面后将它交给这里完成 UID 绑定。
    Q_INVOKABLE void bindLocalVideoSink(QObject *sinkObject);
    Q_INVOKABLE void unbindLocalVideoSink(QObject *sinkObject);
    Q_INVOKABLE void bindRemoteVideoSink(int publisherUid, QObject *sinkObject);
    Q_INVOKABLE void unbindRemoteVideoSink(int publisherUid, QObject *sinkObject);

signals:
    void cameraEnabledChanged();
    void microphoneEnabledChanged();
    void localVideoAvailableChanged();
    void mediaError(const QString &message);
    // 视频已经根据对应成员的音频主时钟完成调度，上层可直接交给 QVideoSink 显示。
    void remoteVideoFrameReady(int publisherUid, const QVideoFrame &frame);

private:
    void slotLocalOfferReady(const QString &meetingId, const QString &sdp);
    void slotLocalAnswerReady(const QString &meetingId, const QString &sdp);
    void slotLocalCandidateReady(const QString &meetingId, const QString &candidate,
                                 const QString &mid);
    void slotRemoteVideoEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                          quint32 rtpTimestamp);
    void slotRemoteAudioEncodedFrameReady(int publisherUid, const QByteArray &frame,
                                          quint32 rtpTimestamp);
    RemoteMediaReceiver *receiverFor(int publisherUid);

    std::unique_ptr<MediaDeviceCapture> m_deviceCapture;
    std::unique_ptr<MediaSession> m_mediaSession;
    std::unique_ptr<MediaStreamProcessor> m_streamProcessor;
    std::unique_ptr<RemoteVideoRenderer> m_videoRenderer;
    std::unordered_map<int, std::unique_ptr<RemoteMediaReceiver>> m_remoteReceivers;
    bool m_cameraEnabled = false;
    bool m_microphoneEnabled = false;
};

#endif // MEDIACONTROLLER_H
