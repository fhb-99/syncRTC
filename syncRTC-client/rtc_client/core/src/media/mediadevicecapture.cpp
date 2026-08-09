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

    const QCameraDevice device = QMediaDevices::defaultVideoInput();
    if (device.isNull()) {
        qWarning() << "No camera device available";
        return false;
    }

    m_videoSink = std::make_unique<QVideoSink>();
    m_camera = std::make_unique<QCamera>(device);
    m_captureSession->setCamera(m_camera.get());
    m_captureSession->setVideoSink(m_videoSink.get());

    // 这里只拉起本地采集链路，后续编码/发送可以从 videoSink 继续接入。
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

    QAudioFormat format = device.preferredFormat();
    if (!format.isValid()) {
        format.setSampleRate(48000);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
    }

    m_audioSource = std::make_unique<QAudioSource>(device, format);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        m_audioSource.reset();
        return false;
    }

    // 当前阶段先把麦克风数据读出并释放，避免缓冲堆积；编码发送后续再接。
    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (m_audioDevice) {
            m_audioDevice->readAll();
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
