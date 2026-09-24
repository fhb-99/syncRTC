#include "mediacontroller.h"

#include "audioencodeworker.h"
#include "mediadevicecapture.h"
#include "rtctransportworker.h"
#include "videoencodeworker.h"
#include "receive/remotemediareceiver.h"
#include "render/remotevideorenderer.h"
#include "../network/tcpmgr.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <QVideoSink>

MediaController::MediaController(QObject *parent)
    : QObject{parent}
    , m_deviceCapture(std::make_unique<MediaDeviceCapture>(this))
    , m_rtcTransportThread(std::make_unique<QThread>())
    , m_audioEncodeThread(std::make_unique<QThread>())
    , m_videoEncodeThread(std::make_unique<QThread>())
    , m_videoRenderer(std::make_unique<RemoteVideoRenderer>(this))
{
    // PeerConnection、Track 生命周期和全部 Track::send() 收拢到一个线程。这样 FFmpeg
    // 音视频编码线程只生产 RTP，不会并发访问 libdatachannel 的同一条发送链路。
    m_rtcTransportWorker = new RtcTransportWorker;
    m_rtcTransportWorker->moveToThread(m_rtcTransportThread.get());
    connect(m_rtcTransportThread.get(), &QThread::finished,
            m_rtcTransportWorker, &QObject::deleteLater);
    m_rtcTransportThread->start();

    // 音频 worker 与视频 worker 相同，不能带 parent 后移动线程。
    // 麦克风 PCM 的缓冲、Opus 上下文和 RTP 打包状态都由它独占。
    m_audioEncodeWorker = new AudioEncodeWorker;
    m_audioEncodeWorker->setTransportWorker(m_rtcTransportWorker);
    m_audioEncodeWorker->moveToThread(m_audioEncodeThread.get());
    connect(m_audioEncodeThread.get(), &QThread::finished,
            m_audioEncodeWorker, &QObject::deleteLater);
    m_audioEncodeThread->start();

    // 视频 worker 不设置 parent 后再 moveToThread，避免 QObject 父对象跨线程的问题。
    // 所有 FFmpeg 视频资源均在该线程创建、使用和释放。
    m_videoEncodeWorker = new VideoEncodeWorker;
    m_videoEncodeWorker->setTransportWorker(m_rtcTransportWorker);
    m_videoEncodeWorker->moveToThread(m_videoEncodeThread.get());
    connect(m_videoEncodeThread.get(), &QThread::finished,
            m_videoEncodeWorker, &QObject::deleteLater);
    m_videoEncodeThread->start();

    // 媒体协商仍由主线程沿现有 TCP 控制链路发送；RTC worker 只负责产生 SDP、ICE。
    connect(m_rtcTransportWorker, &RtcTransportWorker::localOfferReady,
            this, &MediaController::slotLocalOfferReady);
    connect(m_rtcTransportWorker, &RtcTransportWorker::localAnswerReady,
            this, &MediaController::slotLocalAnswerReady);
    connect(m_rtcTransportWorker, &RtcTransportWorker::localCandidateReady,
            this, &MediaController::slotLocalCandidateReady);

    // 得到其他用户的音视频数据
    connect(m_rtcTransportWorker, &RtcTransportWorker::remoteVideoEncodedFrameReady,
            this, &MediaController::slotRemoteVideoEncodedFrameReady);
    connect(m_rtcTransportWorker, &RtcTransportWorker::remoteAudioEncodedFrameReady,
            this, &MediaController::slotRemoteAudioEncodedFrameReady);

    // 本地采集到的原始视频帧数据直接渲染
    connect(m_deviceCapture.get(), &MediaDeviceCapture::videoFrameCaptured,
            m_videoRenderer.get(), &RemoteVideoRenderer::renderLocalFrame);
    connect(m_deviceCapture.get(), &MediaDeviceCapture::videoFrameCaptured,
            this, &MediaController::slotVideoFrameCaptured);
    connect(m_deviceCapture.get(), &MediaDeviceCapture::audioPcmDataCaptured,
            this, &MediaController::slotAudioPcmDataCaptured);

    // 渲染远程视频帧数据
    connect(this, &MediaController::remoteVideoFrameReady,
            m_videoRenderer.get(), &RemoteVideoRenderer::renderRemoteFrame);

    connect(m_videoRenderer.get(), &RemoteVideoRenderer::localVideoAvailableChanged,
            this, &MediaController::localVideoAvailableChanged);
}

MediaController::~MediaController()
{
    // 销毁前在各自线程释放 FFmpeg 资源，再退出线程，不能由主线程直接释放。
    QMetaObject::invokeMethod(m_audioEncodeWorker, &AudioEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    m_audioEncodeThread->quit();
    m_audioEncodeThread->wait();
    m_audioEncodeWorker = nullptr;

    QMetaObject::invokeMethod(m_videoEncodeWorker, &VideoEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    m_videoEncodeThread->quit();
    m_videoEncodeThread->wait();
    m_videoEncodeWorker = nullptr;

    // 编码线程已完全退出，不会再向传输队列提交 RTP；此时才关闭 PeerConnection 并退出 RTC 线程。
    QMetaObject::invokeMethod(m_rtcTransportWorker, &RtcTransportWorker::stopMediaSession,
                              Qt::BlockingQueuedConnection);
    m_rtcTransportThread->quit();
    m_rtcTransportThread->wait();
    m_rtcTransportWorker = nullptr;
}

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

    // 主线程只解析业务字段，PeerConnection 的操作排入 RTC 线程，避免与 Track::send 并发。
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker, sdp, type]() {
        transportWorker->setRemoteDescription(sdp, type);
    }, Qt::QueuedConnection);
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
    // RTC worker 设置远端描述后会立即创建本地 Answer，并通过 localAnswerReady 返回控制层。
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker, sdp]() {
        transportWorker->setRemoteOffer(sdp);
    }, Qt::QueuedConnection);
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
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker, candidate, mid]() {
        transportWorker->addRemoteCandidate(candidate, mid);
    }, Qt::QueuedConnection);
    return true;
}

void MediaController::slotLocalOfferReady(const QString &meetingId, const QString &sdp)
{
    qInfo().noquote() << "[local-offer-send]"
                      << "meeting=" << meetingId
                      << "sdp_bytes=" << sdp.toUtf8().size()
                      << "has_audio=" << sdp.contains("m=audio")
                      << "has_video=" << sdp.contains("m=video")
                      << "has_ice_ufrag=" << sdp.contains("a=ice-ufrag:")
                      << "has_ice_pwd=" << sdp.contains("a=ice-pwd:")
                      << "has_fingerprint=" << sdp.contains("a=fingerprint:");


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

void MediaController::slotAudioPcmDataCaptured(const QByteArray &pcmData)
{
    // 主线程仅将采集 PCM 入队；20ms 分帧、采样格式转换、Opus 编码和 RTP 封装均在音频线程完成。
    m_audioEncodeWorker->enqueueAudioPcmData(pcmData);
}

void MediaController::slotVideoFrameCaptured(const QVideoFrame &frame)
{
    // 此处运行在主线程，只把最新帧交给 worker；颜色转换、编码和 RTP 封装都不在此执行。
    m_videoEncodeWorker->enqueueVideoFrame(frame);
}

void MediaController::requestOpenCamera(const QString &meetingId)
{
    if (m_cameraEnabled) {
        return;
    }

    // 视频编码器在独立线程初始化；完成后才启动采集，避免第一帧落到未初始化的编码器上。
    QMetaObject::invokeMethod(m_videoEncodeWorker, &VideoEncodeWorker::start,
                              Qt::BlockingQueuedConnection);

    if (!m_deviceCapture->startCamera()) {
        QMetaObject::invokeMethod(m_videoEncodeWorker, &VideoEncodeWorker::stop,
                                  Qt::BlockingQueuedConnection);
        emit mediaError(QStringLiteral("摄像头启动失败，请检查设备或权限"));
        return;
    }

    // 创建媒体会话并打开视频发送资格。阻塞等待仅覆盖一次会话初始化，不让首帧落到未创建
    // Track 的旧发送链路；SDP/ICE 仍然由信号异步交给主线程和 TcpMgr。
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker, meetingId]() {
        transportWorker->startMediaSession(meetingId);
        transportWorker->setVideoSendingEnabled(true);
    }, Qt::BlockingQueuedConnection);
    m_cameraEnabled = true;
    emit cameraEnabledChanged();
}

void MediaController::requestCloseCamera(const QString &meetingId)
{
    Q_UNUSED(meetingId)
    if (!m_cameraEnabled) {
        return;
    }

    // 先停止视频 worker 并清空其单帧槽，再让 RTC 线程清空视频 RTP，保证不会继续发送旧画面。
    QMetaObject::invokeMethod(m_videoEncodeWorker, &VideoEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    m_deviceCapture->stopCamera();
    m_videoRenderer->clearLocalFrame();
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker]() {
        transportWorker->setVideoSendingEnabled(false);
    }, Qt::BlockingQueuedConnection);
    m_cameraEnabled = false;
    emit cameraEnabledChanged();
}

void MediaController::requestOpenMicrophone()
{
    if (m_microphoneEnabled) {
        return;
    }

    // 先在独立线程准备 PCM 缓冲和 Opus 编码器，再启动麦克风采集。
    QMetaObject::invokeMethod(m_audioEncodeWorker, &AudioEncodeWorker::start,
                              Qt::BlockingQueuedConnection);

    if (!m_deviceCapture->startMicrophone()) {
        QMetaObject::invokeMethod(m_audioEncodeWorker, &AudioEncodeWorker::stop,
                                  Qt::BlockingQueuedConnection);
        emit mediaError(QStringLiteral("麦克风启动失败，请检查设备或权限"));
        return;
    }

    // 即使摄像头尚未打开，也只记录音频发送资格；真正的 Track 会在随后创建会话时由
    // RTC worker 统一持有，避免主线程保存和切换 Track。
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker]() {
        transportWorker->setAudioSendingEnabled(true);
    }, Qt::BlockingQueuedConnection);
    m_microphoneEnabled = true;
    emit microphoneEnabledChanged();
}

void MediaController::requestCloseMicrophone()
{
    if (!m_microphoneEnabled) {
        return;
    }

    // 先停止音频 worker 并丢弃待编码 PCM，再清空 RTC 队列，避免停止后继续发送旧语音。
    QMetaObject::invokeMethod(m_audioEncodeWorker, &AudioEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    m_deviceCapture->stopMicrophone();
    RtcTransportWorker *transportWorker = m_rtcTransportWorker;
    QMetaObject::invokeMethod(transportWorker, [transportWorker]() {
        transportWorker->setAudioSendingEnabled(false);
    }, Qt::BlockingQueuedConnection);
    m_microphoneEnabled = false;
    emit microphoneEnabledChanged();
}

void MediaController::requestStopAll()
{
    // 两个编码线程停止确认后才释放 PeerConnection，避免它们继续向已关闭的传输线程提交 RTP。
    QMetaObject::invokeMethod(m_videoEncodeWorker, &VideoEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_audioEncodeWorker, &AudioEncodeWorker::stop,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_rtcTransportWorker, &RtcTransportWorker::stopMediaSession,
                              Qt::BlockingQueuedConnection);
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
