#include "mediacontroller.h"

#include "mediadevicecapture.h"
#include "mediasession.h"
#include "mediastreamprocessor.h"
#include "../network/mediatransportmgr.h"
#include "../network/tcpmgr.h"

#include <QJsonDocument>
#include <QJsonObject>

MediaController::MediaController(QObject *parent)
    : QObject{parent}
    , m_deviceCapture(std::make_unique<MediaDeviceCapture>(this))
    , m_mediaSession(std::make_unique<MediaSession>(this))
    , m_streamProcessor(std::make_unique<MediaStreamProcessor>(this))
{
    connect(m_mediaSession.get(), &MediaSession::localOfferReady,
            this, &MediaController::slotLocalOfferReady);
    connect(m_mediaSession.get(), &MediaSession::localCandidateReady,
            this, &MediaController::slotLocalCandidateReady);
}

MediaController::~MediaController() = default;

bool MediaController::applyMediaAnswer(const QJsonObject &json)
{
    // answer 是对端返回的 SDP，会告诉本地 PeerConnection 最终采用的媒体参数。
    const QString sdp = json.value("sdp").toString();
    const QString type = json.value("type").toString(QStringLiteral("answer"));
    if (sdp.isEmpty()) {
        return false;
    }

    // MediaController 只解析业务字段，真正的 WebRTC 对象操作交给 MediaSession。
    m_mediaSession->setRemoteDescription(sdp, type);
    return true;
}

bool MediaController::applyMediaCandidate(const QJsonObject &json)
{
    // candidate 是 ICE 候选地址，用来告诉 PeerConnection 可以尝试哪条网络路径。
    const QString candidate = json.value("candidate").toString();
    const QString mid = json.value("mid").toString();
    if (candidate.isEmpty() || mid.isEmpty()) {
        return false;
    }

    // mid 用来标识 candidate 属于哪一路媒体，例如 video 或 audio。
    m_mediaSession->addRemoteCandidate(candidate, mid);
    return true;
}

void MediaController::slotLocalOfferReady(const QString &meetingId, const QString &sdp)
{
    QJsonObject request;
    request["meeting_id"] = meetingId;
    request["type"] = QStringLiteral("offer");
    request["sdp"] = sdp;

    // offer 属于媒体协商信令，仍然复用现有 RealtimeServer TCP 控制链路发送。
    TcpMgr::GetInstance()->signal_send_data(
        ID_MEDIA_OFFER_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MediaController::slotLocalCandidateReady(const QString &meetingId,
                                              const QString &candidate,
                                              const QString &mid)
{
    QJsonObject request;
    request["meeting_id"] = meetingId;
    request["candidate"] = candidate;
    request["mid"] = mid;

    // candidate 同样只走控制链路，真正音视频数据不经过 RealtimeServer。
    TcpMgr::GetInstance()->signal_send_data(
        ID_MEDIA_CANDIDATE_REQUEST, QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MediaController::requestOpenCamera(const QString &meetingId)
{
    if (m_cameraEnabled) {
        return;
    }

    connect(m_deviceCapture.get(), &MediaDeviceCapture::videoFrameCaptured,
            m_streamProcessor.get(),
            static_cast<void (MediaStreamProcessor::*)(const QVideoFrame &)>(
                &MediaStreamProcessor::processVideoFrame),
            Qt::UniqueConnection);

    // 开启视频编码和封装处理，采集到的帧会通过上面的信号进入处理器。
    m_streamProcessor->startVideo();

    if (!m_deviceCapture->startCamera()) {
        m_streamProcessor->stopVideo();
        emit mediaError(QStringLiteral("摄像头启动失败，请检查设备或权限"));
        return;
    }

    // 创建媒体会话并生成 offer/candidate，仍然通过 TcpMgr 走控制链路发送。
    m_mediaSession->startMediaSession(meetingId);

    MediaTransportMgr::GetInstance()->setVideoTrack(m_mediaSession->videoTrack());
    m_cameraEnabled = true;
    emit cameraEnabledChanged();
}

void MediaController::requestCloseCamera(const QString &meetingId)
{
    Q_UNUSED(meetingId)
    if (!m_cameraEnabled) {
        return;
    }

    m_streamProcessor->stopVideo();
    m_deviceCapture->stopCamera();
    MediaTransportMgr::GetInstance()->clearVideoTrack();
    m_cameraEnabled = false;
    emit cameraEnabledChanged();
}

void MediaController::requestOpenMicrophone()
{
    if (m_microphoneEnabled) {
        return;
    }

    connect(m_deviceCapture.get(), &MediaDeviceCapture::audioPcmDataCaptured,
            m_streamProcessor.get(), &MediaStreamProcessor::processAudioPcmData,
            Qt::UniqueConnection);

    // 先准备音频处理侧的 PCM 缓冲区，再启动麦克风采集。
    m_streamProcessor->startAudio();

    if (!m_deviceCapture->startMicrophone()) {
        m_streamProcessor->stopAudio();
        emit mediaError(QStringLiteral("麦克风启动失败，请检查设备或权限"));
        return;
    }

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
    MediaTransportMgr::GetInstance()->clearVideoTrack();
    m_mediaSession->stopMediaSession();
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
