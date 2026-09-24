#include "rtctransportworker.h"

#include "mediasession.h"

#include <QMetaObject>

RtcTransportWorker::RtcTransportWorker(QObject *parent)
    : QObject(parent)
    , m_mediaSession(new MediaSession(this))
{
    // MediaSession 是本 worker 的子对象；worker moveToThread 后会一起迁移，所有 RTC API
    // 调用都会落在同一线程。它从 libdatachannel 回调得到的数据再由信号交给主线程。
    connect(m_mediaSession, &MediaSession::localOfferReady,
            this, &RtcTransportWorker::localOfferReady);
    connect(m_mediaSession, &MediaSession::localAnswerReady,
            this, &RtcTransportWorker::localAnswerReady);
    connect(m_mediaSession, &MediaSession::localCandidateReady,
            this, &RtcTransportWorker::localCandidateReady);
    connect(m_mediaSession, &MediaSession::remoteVideoEncodedFrameReady,
            this, &RtcTransportWorker::remoteVideoEncodedFrameReady);
    connect(m_mediaSession, &MediaSession::remoteAudioEncodedFrameReady,
            this, &RtcTransportWorker::remoteAudioEncodedFrameReady);
}

RtcTransportWorker::~RtcTransportWorker()
{
    stopMediaSession();
}

void RtcTransportWorker::enqueueVideoRtpPackets(const QVector<QByteArray> &packets)
{
    if (packets.isEmpty()) {
        return;
    }

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // 视频来不及发送时直接丢弃完整旧帧，优先让远端尽快看到最新画面。
        if (m_videoRtpFrames.size() >= MaxQueuedVideoFrames) {
            m_videoRtpFrames.dequeue();
        }
        m_videoRtpFrames.enqueue(packets);
        if (!m_sendScheduled) {
            m_sendScheduled = true;
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        QMetaObject::invokeMethod(this, &RtcTransportWorker::processPendingRtp,
                                  Qt::QueuedConnection);
    }
}

void RtcTransportWorker::enqueueAudioRtpPacket(const QByteArray &packet)
{
    if (packet.isEmpty()) {
        return;
    }

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        // 三个 20ms 音频包约为 60ms。积压时丢最旧包，不能继续发送历史语音。
        if (m_audioRtpPackets.size() >= MaxQueuedAudioPackets) {
            m_audioRtpPackets.dequeue();
        }
        m_audioRtpPackets.enqueue(packet);
        if (!m_sendScheduled) {
            m_sendScheduled = true;
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        QMetaObject::invokeMethod(this, &RtcTransportWorker::processPendingRtp,
                                  Qt::QueuedConnection);
    }
}

void RtcTransportWorker::startMediaSession(const QString &meetingId)
{
    if (m_mediaSessionStarted) {
        return;
    }

    m_mediaSession->startMediaSession(meetingId);
    m_mediaSessionStarted = true;
}

void RtcTransportWorker::stopMediaSession()
{
    // 先让所有新 RTP 在入队后失去发送资格，再清空队列并释放 Track/PeerConnection。
    m_videoSendingEnabled = false;
    m_audioSendingEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_videoRtpFrames.clear();
        m_audioRtpPackets.clear();
    }
    if (m_mediaSessionStarted) {
        m_mediaSession->stopMediaSession();
        m_mediaSessionStarted = false;
    }
}

void RtcTransportWorker::setRemoteDescription(const QString &sdp, const QString &type)
{
    m_mediaSession->setRemoteDescription(sdp, type);
}

void RtcTransportWorker::setRemoteOffer(const QString &sdp)
{
    m_mediaSession->setRemoteOffer(sdp);
}

void RtcTransportWorker::addRemoteCandidate(const QString &candidate, const QString &mid)
{
    m_mediaSession->addRemoteCandidate(candidate, mid);
}

void RtcTransportWorker::setVideoSendingEnabled(bool enabled)
{
    m_videoSendingEnabled = enabled;
    if (!enabled) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_videoRtpFrames.clear();
    }
}

void RtcTransportWorker::setAudioSendingEnabled(bool enabled)
{
    m_audioSendingEnabled = enabled;
    if (!enabled) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_audioRtpPackets.clear();
    }
}

void RtcTransportWorker::processPendingRtp()
{
    while (true) {
        QByteArray audioPacket;
        QVector<QByteArray> videoPackets;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            // 音频严格优先，避免关键帧的大量视频 RTP 包拖慢 20ms 语音包。
            if (!m_audioRtpPackets.isEmpty()) {
                audioPacket = m_audioRtpPackets.dequeue();
            } else if (!m_videoRtpFrames.isEmpty()) {
                videoPackets = m_videoRtpFrames.dequeue();
            } else {
                m_sendScheduled = false;
                return;
            }
        }

        if (!audioPacket.isEmpty()) {
            if (m_audioSendingEnabled) {
                m_mediaSession->sendAudioRtp(audioPacket);
            }
            continue;
        }

        if (m_videoSendingEnabled) {
            for (const QByteArray &packet : videoPackets) {
                m_mediaSession->sendVideoRtp(packet);
            }
        }
    }
}
