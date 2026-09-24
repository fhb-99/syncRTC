#include "videoencodeworker.h"

#include "rtctransportworker.h"

#include <QMetaObject>
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

VideoEncodeWorker::VideoEncodeWorker(QObject *parent)
    : QObject(parent)
{
}

VideoEncodeWorker::~VideoEncodeWorker()
{
    stop();
}

void VideoEncodeWorker::setTransportWorker(RtcTransportWorker *transportWorker)
{
    m_transportWorker = transportWorker;
}

void VideoEncodeWorker::enqueueVideoFrame(const QVideoFrame &frame)
{
    if (!m_acceptingFrames.load() || !frame.isValid()) {
        return;
    }

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        // 只保留最新帧。若编码速度落后于采集速度，旧帧已经没有实时显示价值。
        m_pendingFrame = frame;
        if (!m_frameScheduled) {
            m_frameScheduled = true;
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        QMetaObject::invokeMethod(this, &VideoEncodeWorker::processNextFrame,
                                  Qt::QueuedConnection);
    }
}

void VideoEncodeWorker::start()
{
    if (m_videoStarted) {
        return;
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    m_videoCodecCtx = avcodec_alloc_context3(codec);
    m_videoCodecCtx->width = 1280;
    m_videoCodecCtx->height = 720;
    m_videoCodecCtx->time_base = AVRational{1, 30};
    m_videoCodecCtx->framerate = AVRational{30, 1};
    m_videoCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_videoCodecCtx->bit_rate = 1200 * 1000;
    m_videoCodecCtx->gop_size = 60;
    m_videoCodecCtx->max_b_frames = 0;

    av_opt_set(m_videoCodecCtx->priv_data, "preset", "veryfast", 0);
    av_opt_set(m_videoCodecCtx->priv_data, "tune", "zerolatency", 0);
    avcodec_open2(m_videoCodecCtx, codec, nullptr);

    m_videoPacket = av_packet_alloc();
    m_videoFrame = av_frame_alloc();
    m_videoFrame->format = m_videoCodecCtx->pix_fmt;
    m_videoFrame->width = m_videoCodecCtx->width;
    m_videoFrame->height = m_videoCodecCtx->height;
    av_frame_get_buffer(m_videoFrame, 32);

    const uint32_t videoSsrc = 123456;
    const uint8_t payloadType = 96;
    const std::string cname = "video";
    m_videoRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        videoSsrc, cname, payloadType, rtc::H264RtpPacketizer::ClockRate);
    m_videoPacketizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::StartSequence, m_videoRtpConfig);

    m_videoPts = 0;
    m_videoTimestamp = 0;
    m_videoStarted = true;
    m_acceptingFrames.store(true);
}

void VideoEncodeWorker::stop()
{
    m_acceptingFrames.store(false);
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame = QVideoFrame();
        m_frameScheduled = false;
    }

    if (!m_videoStarted) {
        return;
    }

    // 编码器、缩放器和 RTP 打包器均只会在本线程创建、使用和释放，避免跨线程释放。
    sws_freeContext(m_swsContext);
    m_swsContext = nullptr;
    av_frame_free(&m_videoFrame);
    av_packet_free(&m_videoPacket);
    avcodec_free_context(&m_videoCodecCtx);
    m_videoPacketizer.reset();
    m_videoRtpConfig.reset();

    m_videoPts = 0;
    m_videoTimestamp = 0;
    m_videoStarted = false;
}

void VideoEncodeWorker::processNextFrame()
{
    QVideoFrame frame;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_frameScheduled = false;
        if (!m_acceptingFrames.load() || !m_pendingFrame.isValid()) {
            return;
        }
        frame = m_pendingFrame;
        m_pendingFrame = QVideoFrame();
    }

    processVideoFrame(frame);
}

void VideoEncodeWorker::processVideoFrame(const QVideoFrame &frame)
{
    if (!m_videoStarted) {
        return;
    }

    QVideoFrame mappedFrame(frame);
    if (!mappedFrame.map(QVideoFrame::ReadOnly)) {
        return;
    }

    const AVPixelFormat srcFormat = qtPixelFormatToAvPixelFormat(mappedFrame.pixelFormat());
    if (srcFormat == AV_PIX_FMT_NONE || av_frame_make_writable(m_videoFrame) < 0) {
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

    // 输入尺寸或格式没有变化时，sws_getCachedContext 直接复用旧上下文，避免每帧创建和销毁。
    m_swsContext = sws_getCachedContext(m_swsContext,
                                        mappedFrame.width(),
                                        mappedFrame.height(),
                                        srcFormat,
                                        m_videoCodecCtx->width,
                                        m_videoCodecCtx->height,
                                        m_videoCodecCtx->pix_fmt,
                                        SWS_BILINEAR,
                                        nullptr,
                                        nullptr,
                                        nullptr);
    if (!m_swsContext) {
        mappedFrame.unmap();
        return;
    }

    sws_scale(m_swsContext,
              srcData,
              srcLineSize,
              0,
              mappedFrame.height(),
              m_videoFrame->data,
              m_videoFrame->linesize);
    mappedFrame.unmap();

    sendVideoFrame(m_videoFrame);
}

void VideoEncodeWorker::sendVideoFrame(AVFrame *frame)
{
    frame->pts = m_videoPts++;
    avcodec_send_frame(m_videoCodecCtx, frame);

    while (avcodec_receive_packet(m_videoCodecCtx, m_videoPacket) == 0) {
        const QByteArray h264Frame(reinterpret_cast<const char *>(m_videoPacket->data),
                                   m_videoPacket->size);
        const QVector<QByteArray> rtpPackets = packetizeH264Frame(h264Frame);
        // 一帧 H.264 可能被拆成多个 FU-A RTP 包，必须整体交给传输线程。这样传输队列
        // 在拥塞时只会丢弃完整视频帧，不会留下无法解码的半帧。
        m_transportWorker->enqueueVideoRtpPackets(rtpPackets);
        av_packet_unref(m_videoPacket);
    }
}

QVector<QByteArray> VideoEncodeWorker::packetizeH264Frame(const QByteArray &h264Frame)
{
    QVector<QByteArray> rtpPackets;
    if (h264Frame.isEmpty() || !m_videoPacketizer) {
        return rtpPackets;
    }

    rtc::binary sample(reinterpret_cast<const rtc::byte *>(h264Frame.constData()),
                       reinterpret_cast<const rtc::byte *>(h264Frame.constData()
                                                           + h264Frame.size()));
    auto frameInfo = std::make_shared<rtc::FrameInfo>(m_videoTimestamp);
    rtc::message_vector messages;
    messages.push_back(rtc::make_message(sample.begin(), sample.end(), frameInfo));
    m_videoPacketizer->outgoing(messages, [](rtc::message_ptr) {});

    for (const rtc::message_ptr &message : messages) {
        rtpPackets.append(QByteArray(reinterpret_cast<const char *>(message->data()),
                                     int(message->size())));
    }

    // 30fps 下视频 RTP 时钟每帧前进 90000 / 30 = 3000。
    m_videoTimestamp += 3000;
    return rtpPackets;
}
