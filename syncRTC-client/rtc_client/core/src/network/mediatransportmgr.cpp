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

    // MediaSession持有Track的生命周期，这里只保存弱引用，会议结束后不会误用已经释放的PeerConnection。
    const auto track = m_videoTrack.lock();
    if (!track || !track->isOpen()) {
        // answer和ICE尚未完成时Track还未打开。实时媒体没有补发价值，直接丢弃当前包即可。
        return;
    }

    // packet已经是完整RTP包；Track会通过当前PeerConnection把它加密后发送到MediaServer。
    track->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                static_cast<size_t>(packet.size()));
}

void MediaTransportMgr::sendAudioRtp(const QByteArray &packet)
{
    if (packet.isEmpty()) {
        return;
    }

    // 音频与视频共用同一个PeerConnection，但分别写入各自协商出来的媒体Track。
    const auto track = m_audioTrack.lock();
    if (!track || !track->isOpen()) {
        // 连接建立前不缓存PCM或RTP，避免连接成功后播放已经过期的声音。
        return;
    }

    // 音频编码和RTP封装已经在MediaStreamProcessor完成，这里只负责交给WebRTC传输层。
    track->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                static_cast<size_t>(packet.size()));
}
