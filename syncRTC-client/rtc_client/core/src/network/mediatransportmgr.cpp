#include "mediatransportmgr.h"

MediaTransportMgr::MediaTransportMgr(QObject *parent)
    : QObject(parent)
{
}

void MediaTransportMgr::sendVideoRtp(const QByteArray &packet)
{
    Q_UNUSED(packet)
    // 预留：后续只负责把视频 RTP 包写入已经建立好的 WebRTC track。
}

void MediaTransportMgr::sendAudioRtp(const QByteArray &packet)
{
    Q_UNUSED(packet)
    // 预留：后续只负责把音频 RTP 包写入已经建立好的 WebRTC track。
}
