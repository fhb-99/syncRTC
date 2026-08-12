#include "mediasession.h"

#if __has_include(<rtc/rtc.hpp>)
#include <rtc/rtc.hpp>
#else
#include <rtc/rtc.h>
#endif

MediaSession::MediaSession(QObject *parent)
    : QObject(parent)
{
}

MediaSession::~MediaSession() = default;

std::shared_ptr<rtc::Track> MediaSession::videoTrack() const
{
    return m_videoTrack;
}

void MediaSession::startMediaSession(const QString &meetingId)
{
    m_meetingId = meetingId;

    rtc::Configuration config;
    config.disableAutoNegotiation = true;

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onLocalDescription([this](rtc::Description description) {
        emit localOfferReady(m_meetingId,
                             QString::fromStdString(std::string(description)));
    });

    m_peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        emit localCandidateReady(m_meetingId,
                                 QString::fromStdString(std::string(candidate)),
                                 QString::fromStdString(candidate.mid()));
    });

    rtc::Description::Video video("video");
    video.addH264Codec(96);
    video.addSSRC(123456, "video", "syncRTC", "video");

    // 当前只创建视频发送轨道；offer/candidate 仍由上层通过 TcpMgr 发给 RealtimeServer。
    m_videoTrack = m_peerConnection->addTrack(video);
    m_peerConnection->setLocalDescription();
}

void MediaSession::stopMediaSession()
{
    // 预留：关闭 PeerConnection 和本次媒体会话状态。
    m_videoTrack.reset();
    m_peerConnection.reset();
    m_meetingId.clear();
}

void MediaSession::setRemoteDescription(const QString &sdp, const QString &type)
{
    Q_UNUSED(sdp)
    Q_UNUSED(type)
    // 预留：接收 MediaServer 经 RealtimeServer 返回的 answer。
}

void MediaSession::addRemoteCandidate(const QString &candidate, const QString &mid)
{
    Q_UNUSED(candidate)
    Q_UNUSED(mid)
    // 预留：接收 MediaServer 经 RealtimeServer 返回的 ICE candidate。
}
