#include "mediacontroller.h"

#include "mediadevicecapture.h"
#include "mediasession.h"
#include "mediastreamprocessor.h"
#include "receive/remotemediareceiver.h"
#include "render/remotevideorenderer.h"
#include "../network/mediatransportmgr.h"
#include "../network/tcpmgr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVideoSink>

MediaController::MediaController(QObject *parent)
    : QObject{parent}
    , m_deviceCapture(std::make_unique<MediaDeviceCapture>(this))
    , m_mediaSession(std::make_unique<MediaSession>(this))
    , m_streamProcessor(std::make_unique<MediaStreamProcessor>(this))
    , m_videoRenderer(std::make_unique<RemoteVideoRenderer>(this))
{
    connect(m_mediaSession.get(), &MediaSession::localOfferReady,
            this, &MediaController::slotLocalOfferReady);
    connect(m_mediaSession.get(), &MediaSession::localAnswerReady,
            this, &MediaController::slotLocalAnswerReady);
    connect(m_mediaSession.get(), &MediaSession::localCandidateReady,
            this, &MediaController::slotLocalCandidateReady);
    connect(m_mediaSession.get(), &MediaSession::remoteVideoEncodedFrameReady,
            this, &MediaController::slotRemoteVideoEncodedFrameReady);
    connect(m_mediaSession.get(), &MediaSession::remoteAudioEncodedFrameReady,
            this, &MediaController::slotRemoteAudioEncodedFrameReady);
    connect(m_deviceCapture.get(), &MediaDeviceCapture::videoFrameCaptured,
            m_videoRenderer.get(), &RemoteVideoRenderer::renderLocalFrame);
    connect(this, &MediaController::remoteVideoFrameReady,
            m_videoRenderer.get(), &RemoteVideoRenderer::renderRemoteFrame);
    connect(m_videoRenderer.get(), &RemoteVideoRenderer::localVideoAvailableChanged,
            this, &MediaController::localVideoAvailableChanged);
}

MediaController::~MediaController() = default;

bool MediaController::localVideoAvailable() const
{
    return m_videoRenderer->localVideoAvailable();
}

void MediaController::bindLocalVideoSink(QObject *sinkObject)
{
    m_videoRenderer->bindLocalVideoSink(qobject_cast<QVideoSink *>(sinkObject));
}

void MediaController::unbindLocalVideoSink(QObject *sinkObject)
{
    m_videoRenderer->unbindLocalVideoSink(qobject_cast<QVideoSink *>(sinkObject));
}

void MediaController::bindRemoteVideoSink(int publisherUid, QObject *sinkObject)
{
    m_videoRenderer->bindRemoteVideoSink(publisherUid,
                                         qobject_cast<QVideoSink *>(sinkObject));
}

void MediaController::unbindRemoteVideoSink(int publisherUid, QObject *sinkObject)
{
    m_videoRenderer->unbindRemoteVideoSink(publisherUid,
                                           qobject_cast<QVideoSink *>(sinkObject));
}

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

bool MediaController::applyMediaOffer(const QJsonObject &json)
{
    const QString sdp = json.value("sdp").toString();
    const QString type = json.value("type").toString();
    if (sdp.isEmpty() || type != QStringLiteral("offer")) {
        return false;
    }

    // 这份 Offer 由 MediaServer 在新成员加入并新增消费 Track 后主动生成。
    // MediaSession 设置远端描述后会立即创建本地 Answer，并通过 localAnswerReady 返回控制层。
    m_mediaSession->setRemoteOffer(sdp);
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

void MediaController::slotLocalAnswerReady(const QString &meetingId, const QString &sdp)
{
    QJsonObject request;
    request["meeting_id"] = meetingId;
    request["type"] = QStringLiteral("answer");
    request["sdp"] = sdp;

    // Answer 沿原 TCP 控制链路返回 RealtimeServer，再通过 UDS 交给发起 Offer 的 MediaServer。
    TcpMgr::GetInstance()->signal_send_data(
        ID_MEDIA_RENEGOTIATION_ANSWER_REQUEST,
        QJsonDocument(request).toJson(QJsonDocument::Compact));
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

RemoteMediaReceiver *MediaController::receiverFor(int publisherUid)
{
    auto receiver = m_remoteReceivers.find(publisherUid);
    if (receiver == m_remoteReceivers.end()) {
        // 一个远端参会者只创建一个接收对象，它同时管理这个人的视频和音频处理链路。
        // 视频帧和音频帧可能先后到达，因此在第一帧到达时按发布者 UID 创建即可。
        auto remoteReceiver = std::make_unique<RemoteMediaReceiver>(publisherUid);
        connect(remoteReceiver.get(), &RemoteMediaReceiver::videoFrameReady,
                this, &MediaController::remoteVideoFrameReady);
        receiver = m_remoteReceivers.emplace(publisherUid, std::move(remoteReceiver)).first;
    }
    return receiver->second.get();
}

void MediaController::slotRemoteVideoEncodedFrameReady(int publisherUid,
                                                       const QByteArray &frame,
                                                       quint32 rtpTimestamp)
{
    // MediaSession 的 onFrame 回调来自 libdatachannel 收包线程。
    // 该信号通过 Qt 自动队列连接回到 MediaController 所在线程，再按发布者找到接收链路。
    receiverFor(publisherUid)->receiveVideoFrame(frame, rtpTimestamp);
}

void MediaController::slotRemoteAudioEncodedFrameReady(int publisherUid,
                                                       const QByteArray &frame,
                                                       quint32 rtpTimestamp)
{
    // 同一个发布者的音频和视频进入同一个 RemoteMediaReceiver，解码后再汇入其中的
    // AvSyncScheduler。这样每位成员都只使用自己的音频时钟调度自己的视频画面。
    receiverFor(publisherUid)->receiveAudioFrame(frame, rtpTimestamp);
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
    MediaTransportMgr::GetInstance()->setAudioTrack(m_mediaSession->audioTrack());
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
    m_videoRenderer->clearLocalFrame();
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

    MediaTransportMgr::GetInstance()->setAudioTrack(m_mediaSession->audioTrack());
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
    MediaTransportMgr::GetInstance()->clearAudioTrack();
    m_microphoneEnabled = false;
    emit microphoneEnabledChanged();
}

void MediaController::requestStopAll()
{
    MediaTransportMgr::GetInstance()->clearAudioTrack();
    MediaTransportMgr::GetInstance()->clearVideoTrack();
    m_mediaSession->stopMediaSession();
    m_streamProcessor->stopAll();
    m_deviceCapture->stopAll();
    m_videoRenderer->clearFrames();
    // 离开会议后删除所有远端成员的接收上下文，避免下一次会议复用旧成员状态。
    m_remoteReceivers.clear();

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
