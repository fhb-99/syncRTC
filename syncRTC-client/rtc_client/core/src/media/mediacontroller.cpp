#include "mediacontroller.h"

#include "mediadevicecapture.h"
#include "mediastreamprocessor.h"

MediaController::MediaController(QObject *parent)
    : QObject{parent}
    , m_deviceCapture(std::make_unique<MediaDeviceCapture>(this))
    , m_streamProcessor(std::make_unique<MediaStreamProcessor>(this))
{
}

MediaController::~MediaController() = default;

void MediaController::requestOpenCamera()
{
    if (m_cameraEnabled) {
        return;
    }

    if (!m_deviceCapture->startCamera()) {
        emit mediaError(QStringLiteral("摄像头启动失败，请检查设备或权限"));
        return;
    }

    // 后续视频编码和 RTP 封装链路从这里接入，当前 MediaStreamProcessor 只保留接口。
    m_streamProcessor->startVideo();
    m_cameraEnabled = true;
    emit cameraEnabledChanged();
}

void MediaController::requestCloseCamera()
{
    if (!m_cameraEnabled) {
        return;
    }

    m_streamProcessor->stopVideo();
    m_deviceCapture->stopCamera();
    m_cameraEnabled = false;
    emit cameraEnabledChanged();
}

void MediaController::requestOpenMicrophone()
{
    if (m_microphoneEnabled) {
        return;
    }

    if (!m_deviceCapture->startMicrophone()) {
        emit mediaError(QStringLiteral("麦克风启动失败，请检查设备或权限"));
        return;
    }

    // 后续音频编码和 RTP 封装链路从这里接入，当前 MediaStreamProcessor 只保留接口。
    m_streamProcessor->startAudio();
    m_microphoneEnabled = true;
    emit microphoneEnabledChanged();
}

void MediaController::requestCloseMicrophone()
{
    if (!m_microphoneEnabled) {
        return;
    }

    m_streamProcessor->stopAudio();
    m_deviceCapture->stopMicrophone();
    m_microphoneEnabled = false;
    emit microphoneEnabledChanged();
}

void MediaController::requestStopAll()
{
    m_streamProcessor->stopAll();
    m_deviceCapture->stopAll();

    const bool cameraChanged = m_cameraEnabled;
    const bool microphoneChanged = m_microphoneEnabled;
    m_cameraEnabled = false;
    m_microphoneEnabled = false;

    if (cameraChanged) {
        emit cameraEnabledChanged();
    }
    if (microphoneChanged) {
        emit microphoneEnabledChanged();
    }
}
