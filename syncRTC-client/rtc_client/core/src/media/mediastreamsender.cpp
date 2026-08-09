#include "mediastreamsender.h"

MediaStreamSender::MediaStreamSender(QObject *parent)
    : QObject(parent)
{

}

MediaStreamSender::~MediaStreamSender() = default;

void MediaStreamSender::startVideo()
{
    // 预留：视频帧编码、RTP 封装和发送链路后续在这里接入

}

void MediaStreamSender::stopVideo()
{
    // 预留：关闭视频发送链路。
}

void MediaStreamSender::startAudio()
{
    // 预留：音频 PCM 编码、RTP 封装和发送链路后续在这里接入。
}

void MediaStreamSender::stopAudio()
{
    // 预留：关闭音频发送链路。
}

void MediaStreamSender::stopAll()
{
    stopAudio();
    stopVideo();
}
