#include "mediastreamprocessor.h"

MediaStreamProcessor::MediaStreamProcessor(QObject *parent)
    : QObject(parent),
    m_videoCodecCtx(nullptr),
    m_videoPacket(nullptr)
{

}

MediaStreamProcessor::~MediaStreamProcessor() = default;

void MediaStreamProcessor::processVideoFrame(AVFrame *frame)
{

}

void MediaStreamProcessor::startVideo()
{
    // 预留：视频帧编码和 RTP 封装后续在这里接入
    if(m_videoStarted) {
        return;
    }

    // 采用H.264的编码格式
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    m_videoCodecCtx = avcodec_alloc_context3(codec);

    m_videoCodecCtx->width = 1280;
    m_videoCodecCtx->height = 720;
    // 按30帧进行编码
    m_videoCodecCtx->time_base = AVRational{1, 30};
    m_videoCodecCtx->framerate = AVRational{30, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    // 码率
    m_videoCodecCtx->bit_rate = 1200 * 1000;
    // 每60帧就有一个关键帧 I帧 2s一个
    m_videoCodecCtx->gop_size = 60;
    // 为了低延迟，不使用B帧
    m_videoCodecCtx->max_b_frames = 0;

    av_opt_set(m_videoCodecCtx->priv_data, "preset", "veryfast", 0);
    av_opt_set(m_videoCodecCtx->priv_data, "tune", "zerolatency", 0);

    avcodec_open2(m_videoCodecCtx, codec, nullptr);

    m_videoPacket = av_packet_alloc();
    // 初始化视频时间戳和 RTP 序号状态
    m_videoPts = 0;
    m_videoSequence = 0;
    m_videoTimestamp = 0;
    m_videoStarted = true;
}

void MediaStreamProcessor::stopVideo()
{
    // 预留：关闭视频编码和封装链路。
}

void MediaStreamProcessor::startAudio()
{
    // 预留：音频 PCM 编码和 RTP 封装后续在这里接入。
}

void MediaStreamProcessor::stopAudio()
{
    // 预留：关闭音频编码和封装链路。
}

void MediaStreamProcessor::stopAll()
{
    stopAudio();
    stopVideo();
}
