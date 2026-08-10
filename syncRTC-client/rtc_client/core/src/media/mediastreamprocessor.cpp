#include "mediastreamprocessor.h"

#include "../network/mediatransportmgr.h"

#include <QVideoFrameFormat>

namespace {

AVPixelFormat qtPixelFormatToAvPixelFormat(QVideoFrameFormat::PixelFormat format)
{
    switch (format) {
    case QVideoFrameFormat::Format_BGRA8888:
    case QVideoFrameFormat::Format_BGRA8888_Premultiplied:
        return AV_PIX_FMT_BGRA;
    case QVideoFrameFormat::Format_BGRX8888:
        return AV_PIX_FMT_BGR0;
    case QVideoFrameFormat::Format_RGBA8888:
        return AV_PIX_FMT_RGBA;
    case QVideoFrameFormat::Format_RGBX8888:
        return AV_PIX_FMT_RGB0;
    case QVideoFrameFormat::Format_ARGB8888:
    case QVideoFrameFormat::Format_ARGB8888_Premultiplied:
        return AV_PIX_FMT_ARGB;
    case QVideoFrameFormat::Format_XRGB8888:
        return AV_PIX_FMT_0RGB;
    case QVideoFrameFormat::Format_ABGR8888:
        return AV_PIX_FMT_ABGR;
    case QVideoFrameFormat::Format_XBGR8888:
        return AV_PIX_FMT_0BGR;
    case QVideoFrameFormat::Format_NV12:
        return AV_PIX_FMT_NV12;
    case QVideoFrameFormat::Format_NV21:
        return AV_PIX_FMT_NV21;
    case QVideoFrameFormat::Format_YUV420P:
        return AV_PIX_FMT_YUV420P;
    case QVideoFrameFormat::Format_YUV422P:
        return AV_PIX_FMT_YUV422P;
    case QVideoFrameFormat::Format_UYVY:
        return AV_PIX_FMT_UYVY422;
    case QVideoFrameFormat::Format_YUYV:
        return AV_PIX_FMT_YUYV422;
    case QVideoFrameFormat::Format_Y8:
        return AV_PIX_FMT_GRAY8;
    default:
        return AV_PIX_FMT_NONE;
    }
}

}


MediaStreamProcessor::MediaStreamProcessor(QObject *parent)
    : QObject(parent),
    m_videoCodecCtx(nullptr),
    m_videoPacket(nullptr)
{

}

MediaStreamProcessor::~MediaStreamProcessor() = default;

QVector<QByteArray> MediaStreamProcessor::packetizeH264Frame(const QByteArray &h264Frame)
{
    QVector<QByteArray> rtpPackets;

    // 防御：空数据 / 打包器未初始化，直接返回空包列表
    if (h264Frame.isEmpty() || !m_videoPacketizer) {
        return rtpPackets;
    }

    // 1、类型转换：Qt QByteArray → libdatachannel rtc::binary
    // rtc::binary 就是std::vector<std::byte>，libdatachannel内部使用的二进制视图
    rtc::binary sample(
        reinterpret_cast<const rtc::byte *>(h264Frame.constData()),
        reinterpret_cast<const rtc::byte *>(h264Frame.constData() + h264Frame.size())
        );

    // 2、FrameInfo：给这一帧绑定RTP时间戳
    auto frameInfo = std::make_shared<rtc::FrameInfo>(m_videoTimestamp);

    // 3、构造message：libdatachannel用message_ptr承载一个原始媒体样本 + frame信息
    rtc::message_vector messages;
    messages.push_back(rtc::make_message(sample.begin(), sample.end(), frameInfo));

    // =========核心调用 outgoing()=========
    // 输入：原始H264样本（带Annex‑B起始码）
    // 内部做：分割NALU → 判断NALU大小 → 小包单NALU打包、大包FU‑A分片 → 填充RTP头部
    // 输出：messages容器里面，原本1条H.264样本，会被替换成若干条RTP message_ptr
    // 第二个参数是回调，这里我们不需要回调，传空lambda
    m_videoPacketizer->outgoing(messages, [](rtc::message_ptr) {});

    // 4、遍历生成完毕的RTP message，转换成Qt QByteArray存入返回数组
    for (const rtc::message_ptr &message : messages) {
        rtpPackets.append(QByteArray(
            reinterpret_cast<const char *>(message->data()),
            int(message->size())
            ));
    }

    // 30fps，90000/30 = 3000，下一帧时间戳增加
    m_videoTimestamp += 3000;
    return rtpPackets;
}

void MediaStreamProcessor::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_videoStarted || !frame.isValid()) {
        return;
    }

    QVideoFrame mappedFrame(frame);
    if (!mappedFrame.map(QVideoFrame::ReadOnly)) {
        return;
    }

    const AVPixelFormat srcFormat = qtPixelFormatToAvPixelFormat(mappedFrame.pixelFormat());
    if (srcFormat == AV_PIX_FMT_NONE) {
        mappedFrame.unmap();
        return;
    }

    const uint8_t *srcData[4] = {nullptr, nullptr, nullptr, nullptr};
    int srcLineSize[4] = {0, 0, 0, 0};
    const int planeCount = qMin(mappedFrame.planeCount(), 4);
    for (int i = 0; i < planeCount; ++i) {
        srcData[i] = mappedFrame.bits(i);
        srcLineSize[i] = mappedFrame.bytesPerLine(i);
    }

    AVFrame *avFrame = av_frame_alloc();
    avFrame->format = m_videoCodecCtx->pix_fmt;
    avFrame->width = m_videoCodecCtx->width;
    avFrame->height = m_videoCodecCtx->height;

    if (av_frame_get_buffer(avFrame, 32) < 0) {
        av_frame_free(&avFrame);
        mappedFrame.unmap();
        return;
    }

    // Qt 采集帧可能是 NV12/BGRA 等格式，这里统一转成编码器需要的 YUV420P。
    SwsContext *swsContext = sws_getContext(mappedFrame.width(),
                                            mappedFrame.height(),
                                            srcFormat,
                                            m_videoCodecCtx->width,
                                            m_videoCodecCtx->height,
                                            m_videoCodecCtx->pix_fmt,
                                            SWS_BILINEAR,
                                            nullptr,
                                            nullptr,
                                            nullptr);
    if (!swsContext) {
        av_frame_free(&avFrame);
        mappedFrame.unmap();
        return;
    }

    sws_scale(swsContext,
              srcData,
              srcLineSize,
              0,
              mappedFrame.height(),
              avFrame->data,
              avFrame->linesize);

    processVideoFrame(avFrame);

    sws_freeContext(swsContext);
    av_frame_free(&avFrame);
    mappedFrame.unmap();
}

void MediaStreamProcessor::processVideoFrame(AVFrame *frame)
{
    if(!m_videoStarted || frame == nullptr) {
        return;
    }

    frame->pts = m_videoPts++;

    avcodec_send_frame(m_videoCodecCtx, frame);

    while (avcodec_receive_packet(m_videoCodecCtx, m_videoPacket) == 0) {
        // 这里把 m_videoPacket 里的 H.264 数据做 RTP 封装

        // 把AVPacket里面的H.264二进制裸流拷贝到QByteArray
        QByteArray h264Frame(
            reinterpret_cast<const char *>(m_videoPacket->data),
            m_videoPacket->size
        );

        // 这里在 MediaStreamProcessor 内部完成 RTP 封装。
        // 封装可以用 libdatachannel 的 H264RtpPacketizer。
        QVector<QByteArray> rtpPackets = packetizeH264Frame(h264Frame);

        for (const QByteArray &rtpPacket : rtpPackets) {
            MediaTransportMgr::GetInstance()->sendVideoRtp(rtpPacket);
        }

        av_packet_unref(m_videoPacket);
    }
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


    const uint32_t videoSsrc = 123456;        // RTP流SSRC，整条视频流唯一标识
    const uint8_t payloadType = 96;           // RTP payload type，H264动态PT
    const std::string cname = "video";         // RTCP CNAME标识

    // 构造RTP配置
    m_videoRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        videoSsrc,
        cname,
        payloadType,
        rtc::H264RtpPacketizer::ClockRate   // H.264固定90000时钟频率
        );

    // 构造H264打包器
    // 第一个参数：NalUnit::Separator::StartSequence
    // 告诉packetizer：输入是 Annex‑B格式，使用 00 00 01 / 00 00 00 01 作为NALU分隔符
    // 正好匹配FFmpeg avcodec输出的AVPacket原始数据
    m_videoPacketizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence,
        m_videoRtpConfig
        );

    m_videoTimestamp = 0;   // RTP时间戳从0开始
    m_videoStarted = true;
}

void MediaStreamProcessor::stopVideo()
{
    if (!m_videoStarted) {
        return;
    }

    av_packet_free(&m_videoPacket);
    avcodec_free_context(&m_videoCodecCtx);
    m_videoPacketizer.reset();
    m_videoRtpConfig.reset();

    m_videoPts = 0;
    m_videoSequence = 0;
    m_videoTimestamp = 0;
    m_videoStarted = false;
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
