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

std::shared_ptr<rtc::Track> MediaSession::audioTrack() const
{
    return m_audioTrack;
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

    rtc::Description::Audio audio("audio");
    audio.addOpusCodec(111);
    audio.addSSRC(654321, "audio", "syncRTC", "audio");

    // 这两个Track属于客户端与MediaServer之间的PeerConnection。
    // addTrack把固定的编解码器、Payload Type和SSRC写入本地offer；MediaServer返回answer并完成ICE后，
    // Track会进入Open状态。之后写入Track的RTP包会由libdatachannel完成DTLS/SRTP保护并发送给MediaServer。
    // offer/candidate仍由上层通过TcpMgr发给RealtimeServer，它们只是协商信令，不承载音视频数据。
    m_videoTrack = m_peerConnection->addTrack(video);
    m_audioTrack = m_peerConnection->addTrack(audio);
    m_peerConnection->setLocalDescription();
}

void MediaSession::stopMediaSession()
{
    // 释放Track后MediaTransportMgr中的弱引用会自动失效，后续采集包不会再进入旧连接。
    m_videoTrack.reset();
    m_audioTrack.reset();
    m_peerConnection.reset();
    m_meetingId.clear();
}

void MediaSession::setRemoteDescription(const QString &sdp, const QString &type)
{
    // SDP 是整场 WebRTC 协商的说明书：包含编解码器、媒体方向、DTLS 指纹等信息。
    m_peerConnection->setRemoteDescription(
        rtc::Description(sdp.toStdString(), type.toStdString()));
}

void MediaSession::addRemoteCandidate(const QString &candidate, const QString &mid)
{
    // ICE candidate 是对端的可连接地址；设置后 PeerConnection 会尝试打通网络路径。
    m_peerConnection->addRemoteCandidate(
        rtc::Candidate(candidate.toStdString(), mid.toStdString()));
}
