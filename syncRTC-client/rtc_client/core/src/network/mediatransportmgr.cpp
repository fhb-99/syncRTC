#include "mediatransportmgr.h"

#include <rtc/track.hpp>

MediaTransportMgr::MediaTransportMgr(QObject *parent)
    : QObject(parent)
{
}

void MediaTransportMgr::setVideoTrack(const std::shared_ptr<rtc::Track> &track)
{
    m_videoTrack = track;
}

void MediaTransportMgr::setAudioTrack(const std::shared_ptr<rtc::Track> &track)
{
    m_audioTrack = track;
}

void MediaTransportMgr::clearVideoTrack()
{
    m_videoTrack.reset();
}

void MediaTransportMgr::clearAudioTrack()
{
    m_audioTrack.reset();
}

void MediaTransportMgr::sendVideoRtp(const QByteArray &packet)
{
    if (packet.isEmpty()) {
        return;
    }

    const auto track = m_videoTrack.lock();
    if (!track || !track->isOpen()) {
        return;
    }

    track->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                static_cast<size_t>(packet.size()));
}

void MediaTransportMgr::sendAudioRtp(const QByteArray &packet)
{
    if (packet.isEmpty()) {
        return;
    }

    const auto track = m_audioTrack.lock();
    if (!track || !track->isOpen()) {
        return;
    }

    // 音频编码和RTP封装已经在 MediaStreamProcessor 完成，这里只负责写入WebRTC音频轨道。
    track->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                static_cast<size_t>(packet.size()));
}
