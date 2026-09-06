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

constexpr int AudioSampleRate = 48000;
constexpr int AudioFrameSamples = AudioSampleRate * 20 / 1000;
constexpr int AudioFrameBytes = AudioFrameSamples * int(sizeof(qint16));

}


MediaStreamProcessor::MediaStreamProcessor(QObject *parent)
    : QObject(parent),
    m_videoCodecCtx(nullptr),
    m_videoPacket(nullptr),
    m_audioCodecCtx(nullptr),
    m_audioPacket(nullptr)
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

        // 编码器输出的是一帧H.264 Annex-B数据，packetizer会按NALU大小生成一个或多个完整RTP包。
        QVector<QByteArray> rtpPackets = packetizeH264Frame(h264Frame);

        // 每个QByteArray都已经包含RTP头和H.264负载。
        // MediaTransportMgr不再修改媒体内容，只把这些包写入与MediaServer协商好的视频Track。
        for (const QByteArray &rtpPacket : rtpPackets) {
            MediaTransportMgr::GetInstance()->sendVideoRtp(rtpPacket);
        }

        av_packet_unref(m_videoPacket);
    }
}

void MediaStreamProcessor::processAudioPcmData(const QByteArray &pcmData)
{
    if (!m_audioStarted) {
        return;
    }

    // 麦克风 readAll() 每次给的数据长度不固定，先放进缓冲区，攒够20ms再处理。
    m_audioPcmBuffer.append(pcmData);

    while (m_audioPcmBuffer.size() >= AudioFrameBytes) {
        // 20ms单声道Int16 PCM：48000 * 0.02 * 2 = 1920字节。
        QByteArray pcmFrame = m_audioPcmBuffer.left(AudioFrameBytes);
        m_audioPcmBuffer.remove(0, AudioFrameBytes);

        AVFrame *audioFrame = av_frame_alloc();
        audioFrame->nb_samples = AudioFrameSamples;
        audioFrame->format = m_audioCodecCtx->sample_fmt;
        audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
        av_channel_layout_copy(&audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);

        if (av_frame_get_buffer(audioFrame, 0) < 0) {
            av_frame_free(&audioFrame);
            return;
        }

        // 采集侧给的是Int16 PCM，FFmpeg原生Opus编码器需要FLTP，这里转成float平面数据。
        const qint16 *src = reinterpret_cast<const qint16 *>(pcmFrame.constData());
        float *dst = reinterpret_cast<float *>(audioFrame->data[0]);
        for (int i = 0; i < AudioFrameSamples; ++i) {
            dst[i] = float(src[i]) / 32768.0f;
        }

        audioFrame->pts = m_audioPts;
        m_audioPts += audioFrame->nb_samples;

        avcodec_send_frame(m_audioCodecCtx, audioFrame);
        av_frame_free(&audioFrame);

        while (avcodec_receive_packet(m_audioCodecCtx, m_audioPacket) == 0) {
            rtc::binary sample(
                reinterpret_cast<const rtc::byte *>(m_audioPacket->data),
                reinterpret_cast<const rtc::byte *>(m_audioPacket->data + m_audioPacket->size)
                );

            auto frameInfo = std::make_shared<rtc::FrameInfo>(m_audioTimestamp);
            rtc::message_vector messages;
            messages.push_back(rtc::make_message(sample.begin(), sample.end(), frameInfo));

            // Opus音频通常一帧就是一个RTP包，packetizer负责补RTP头、序号和时间戳。
            m_audioPacketizer->outgoing(messages, [](rtc::message_ptr) {});

            // packetizer输出的是完整Opus RTP包，后续直接写入与MediaServer协商好的音频Track。
            for (const rtc::message_ptr &message : messages) {
                MediaTransportMgr::GetInstance()->sendAudioRtp(QByteArray(
                    reinterpret_cast<const char *>(message->data()),
                    int(message->size())
                    ));
            }

            av_packet_unref(m_audioPacket);
        }

        // 48kHz下20ms等于960个采样点，因此RTP时间戳每帧递增960。
        m_audioTimestamp += AudioFrameSamples;
    }
}

void MediaStreamProcessor::startVideo()
{
    // 初始化视频编码器和RTP打包器；采集帧到达后即可在processVideoFrame中完成编码、封装和发送。
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
    if (m_audioStarted) {
        return;
    }

    // WebRTC 音频统一使用 Opus，Opus 的 RTP 时间戳时钟固定按 48000Hz 计算。
    // 根据编码ID查找FFmpeg OPUS编码器实现
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    // 分配音频编码器上下文，承载全部编码参数与编码器运行状态
    m_audioCodecCtx = avcodec_alloc_context3(codec);

    // 采样率：48000Hz，OPUS原生标准采样率，WebRTC音频强制使用48kHz
    m_audioCodecCtx->sample_rate = AudioSampleRate;
    // 音频采样格式：FLTP(float planar)浮点平面格式，FFmpeg OPUS编码器要求的输入格式
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    // 编码码率：32kbps，适合网络语音通话场景
    m_audioCodecCtx->bit_rate = 32000;
    // 时间基 1/48000 s，用于计算音频AVFrame的PTS时间戳
    m_audioCodecCtx->time_base = AVRational{1, AudioSampleRate};
    // 设置声道布局，参数1 = 单声道Mono，WebRTC通话普遍使用单声道节省带宽
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, 1);

    // 根据上面配置的参数打开、初始化OPUS编码器
    avcodec_open2(m_audioCodecCtx, codec, nullptr);

    // 分配AVPacket，用于接收编码器输出的OPUS编码码流
    m_audioPacket = av_packet_alloc();
    // 音频PTS初始计数器，每送入一帧音频采样后递增，标记帧时序
    m_audioPts = 0;
    m_audioTimestamp = 0;

    const uint32_t audioSsrc = 654321;       // RTP流SSRC，整条音频流唯一标识
    const uint8_t payloadType = 111;         // RTP payload type，Opus常用动态PT
    const std::string cname = "audio";       // RTCP CNAME标识

    m_audioRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        audioSsrc,
        cname,
        payloadType,
        rtc::OpusRtpPacketizer::DefaultClockRate
        );
    m_audioPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(m_audioRtpConfig);

    // PCM音频缓冲清空：用来缓存麦克风采集到的零散PCM采样，攒够固定时长(20ms)再编码
    m_audioPcmBuffer.clear();
    // 标记音频编码器初始化完成，允许接收PCM音频数据开始编码
    m_audioStarted = true;
}

void MediaStreamProcessor::stopAudio()
{
    if (!m_audioStarted) {
        return;
    }

    av_packet_free(&m_audioPacket);
    avcodec_free_context(&m_audioCodecCtx);
    m_audioPacketizer.reset();
    m_audioRtpConfig.reset();

    m_audioPts = 0;
    m_audioTimestamp = 0;
    m_audioPcmBuffer.clear();
    m_audioStarted = false;
}

void MediaStreamProcessor::stopAll()
{
    stopAudio();
    stopVideo();
}
