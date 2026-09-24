#include "mediadevicecapture.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QCamera>
#include <QCameraDevice>
#include <QDebug>
#include <QIODevice>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QVideoSink>

MediaDeviceCapture::MediaDeviceCapture(QObject *parent)
    : QObject(parent),
      m_captureSession(std::make_unique<QMediaCaptureSession>())
{
}

MediaDeviceCapture::~MediaDeviceCapture()
{
    stopAll();
}

bool MediaDeviceCapture::startCamera()
{
    if (m_camera) {
        return true;
    }

    QCameraDevice device;
#ifdef Q_OS_ANDROID
    // Android 会议采集固定使用后置摄像头，不回退到前置摄像头。
    const auto videoInputs = QMediaDevices::videoInputs();
    for (const QCameraDevice &candidate : videoInputs) {
        if (candidate.position() == QCameraDevice::BackFace) {
            device = candidate;
            break;
        }
    }
#else
    device = QMediaDevices::defaultVideoInput();
#endif
    if (device.isNull()) {
        qWarning() << "No rear camera device available";
        return false;
    }

    m_videoSink = std::make_unique<QVideoSink>();
    connect(m_videoSink.get(), &QVideoSink::videoFrameChanged,
            this, &MediaDeviceCapture::videoFrameCaptured);

    m_camera = std::make_unique<QCamera>(device);
    m_captureSession->setCamera(m_camera.get());
    m_captureSession->setVideoSink(m_videoSink.get());

    // QVideoSink 只负责把摄像头帧交给 MediaController。
    // 后续会按“像素格式转换 -> H.264编码 -> RTP封装 -> WebRTC视频Track”的顺序发送到MediaServer。
    m_camera->start();
    return true;
}

void MediaDeviceCapture::stopCamera()
{
    if (!m_camera) {
        return;
    }

    m_camera->stop();
    m_captureSession->setCamera(nullptr);
    m_captureSession->setVideoSink(nullptr);
    m_camera.reset();
    m_videoSink.reset();
}

bool MediaDeviceCapture::startMicrophone()
{
    if (m_audioSource) {
        return true;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        qWarning() << "No microphone device available";
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        qWarning() << "Microphone does not support 48000Hz mono Int16 PCM";
        return false;
    }

    m_audioSource = std::make_unique<QAudioSource>(device, format);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        m_audioSource.reset();
        return false;
    }

    // readyRead 每次拿到的数据长度不固定，先把 PCM 交给 MediaController。
    // 后续 AudioEncodeWorker 会在独立线程中按 20ms 切片，完成 Opus 编码和 RTP 封装；
    // 完成的 RTP 包再交给专用 RTC 传输线程写入音频 Track，采集回调不直接触碰网络对象。
    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (m_audioDevice) {
            // readAll() 读到的是麦克风采集出的 PCM 原始音频数据，还没有经过压缩编码。
            emit audioPcmDataCaptured(m_audioDevice->readAll());
        }
    });
    return true;
}

void MediaDeviceCapture::stopMicrophone()
{
    if (!m_audioSource) {
        return;
    }

    m_audioSource->stop();
    m_audioSource.reset();
    m_audioDevice = nullptr;
}

void MediaDeviceCapture::stopAll()
{
    stopMicrophone();
    stopCamera();
}
