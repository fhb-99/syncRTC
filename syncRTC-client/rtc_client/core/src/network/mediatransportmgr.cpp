#include "mediatransportmgr.h"

MediaTransportMgr::MediaTransportMgr(QObject *parent)
    : QObject(parent)
{
}

void MediaTransportMgr::startMediaSession(const QString &meetingId)
{
    Q_UNUSED(meetingId)
    // 预留：后续在这里建立媒体传输会话。
}

void MediaTransportMgr::stopMediaSession()
{
    // 预留：后续在这里关闭媒体传输会话。
}

void MediaTransportMgr::sendVideoRtp(const QByteArray &packet)
{
    Q_UNUSED(packet)
    // 预留：后续在这里发送视频 RTP 包。
}

void MediaTransportMgr::sendAudioRtp(const QByteArray &packet)
{
    Q_UNUSED(packet)
    // 预留：后续在这里发送音频 RTP 包。
}
