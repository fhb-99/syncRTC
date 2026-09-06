#ifndef MEDIADEVICECAPTURE_H
#define MEDIADEVICECAPTURE_H

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>

#include <memory>

class QAudioSource;
class QCamera;
class QIODevice;
class QMediaCaptureSession;
class QVideoSink;

class MediaDeviceCapture : public QObject
{
    Q_OBJECT
public:
    explicit MediaDeviceCapture(QObject *parent = nullptr);
    ~MediaDeviceCapture() override;

    bool startCamera();
    void stopCamera();
    bool startMicrophone();
    void stopMicrophone();
    void stopAll();

signals:
    void videoFrameCaptured(const QVideoFrame &frame);
    void audioPcmDataCaptured(const QByteArray &pcmData);

private:
    std::unique_ptr<QMediaCaptureSession> m_captureSession;
    std::unique_ptr<QCamera> m_camera;
    std::unique_ptr<QVideoSink> m_videoSink;
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_audioDevice = nullptr;
};

#endif // MEDIADEVICECAPTURE_H
