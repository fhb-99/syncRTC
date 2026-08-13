#ifndef MEDIACONTROLLER_H
#define MEDIACONTROLLER_H

#include <QObject>
#include <QString>

#include <memory>

class QJsonObject;
class MediaDeviceCapture;
class MediaSession;
class MediaStreamProcessor;

class MediaController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool cameraEnabled READ cameraEnabled NOTIFY cameraEnabledChanged)
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled NOTIFY microphoneEnabledChanged)
public:
    explicit MediaController(QObject *parent = nullptr);
    ~MediaController() override;

    bool cameraEnabled() const { return m_cameraEnabled; }
    bool microphoneEnabled() const { return m_microphoneEnabled; }

    Q_INVOKABLE void requestOpenCamera(const QString &meetingId);
    Q_INVOKABLE void requestCloseCamera(const QString &meetingId);
    Q_INVOKABLE void requestOpenMicrophone();
    Q_INVOKABLE void requestCloseMicrophone();
    Q_INVOKABLE void requestStopAll();

    Q_INVOKABLE bool applyMediaAnswer(const QJsonObject &json);
    Q_INVOKABLE bool applyMediaCandidate(const QJsonObject &json);

signals:
    void cameraEnabledChanged();
    void microphoneEnabledChanged();
    void mediaError(const QString &message);

private:
    void slotLocalOfferReady(const QString &meetingId, const QString &sdp);
    void slotLocalCandidateReady(const QString &meetingId, const QString &candidate,
                                 const QString &mid);

    std::unique_ptr<MediaDeviceCapture> m_deviceCapture;
    std::unique_ptr<MediaSession> m_mediaSession;
    std::unique_ptr<MediaStreamProcessor> m_streamProcessor;
    bool m_cameraEnabled = false;
    bool m_microphoneEnabled = false;
};

#endif // MEDIACONTROLLER_H
