#include "videoreceivepipeline.h"

#include "../../models/global.h"

#include <QSize>
#include <QVideoFrameFormat>

#include <algorithm>
#include <cstring>

namespace {

// 视频解码只允许少量帧排队。会议场景更关心实时性，解码来不及时丢掉最旧的
// 编码帧，可以避免队列持续增长并把画面延迟累积到数秒以后。
constexpr std::size_t MaxEncodedVideoFrames = 3;

}

VideoReceivePipeline::VideoReceivePipeline(QObject *parent)
    : QObject(parent)
{
    // onFrame 交出的是已经完成 RTP 去头和分片重组的 H.264 Annex-B 数据，
    // 可以直接送给 FFmpeg 解码器，不需要再增加 av_parser_parse2() 解析步骤。
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    m_codecContext = avcodec_alloc_context3(codec);
    m_codecContext->pkt_timebase = AVRational{1, 90000};
    avcodec_open2(m_codecContext, codec, nullptr);

    m_packet = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();

    // FFmpeg 解码不在 libdatachannel 收包线程或 Qt 主线程执行。输入函数只负责
    // 复制编码数据并唤醒本线程，避免解码耗时阻塞后续网络收包和界面事件。
    m_decodeThread = std::thread(&VideoReceivePipeline::decodeLoop, this);
}

VideoReceivePipeline::~VideoReceivePipeline()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopping = true;
    }
    m_queueCondition.notify_one();

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    av_frame_free(&m_decodedFrame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codecContext);
}

void VideoReceivePipeline::receiveEncodedFrame(const QByteArray &frame, quint32 rtpTimestamp)
{
    // 到这里，SRTP 解密、RTP 去头以及 H.264 分片重组都已由 libdatachannel 完成。
    // 此处只把完整编码帧放入有限队列，真正的 FFmpeg 调用全部发生在解码线程。
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_encodedFrames.size() >= MaxEncodedVideoFrames) {
            m_encodedFrames.pop_front();
        }
        m_encodedFrames.push_back(EncodedFrame{frame, rtpTimestamp});
    }
    m_queueCondition.notify_one();
}

void VideoReceivePipeline::decodeLoop()
{
    while (true) {
        EncodedFrame encodedFrame;
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_queueCondition.wait(lock, [this] {
                return m_stopping || !m_encodedFrames.empty();
            });

            if (m_stopping) {
                return;
            }

            encodedFrame = std::move(m_encodedFrames.front());
            m_encodedFrames.pop_front();
        }

        decodeFrame(encodedFrame);
    }
}

void VideoReceivePipeline::decodeFrame(const EncodedFrame &encodedFrame)
{
    // AVPacket 需要持有一份独立内存。encodedFrame 离开本函数后会释放，不能让
    // FFmpeg 继续引用 QByteArray 内部地址，因此先复制到 av_new_packet 分配的缓冲区。
    av_packet_unref(m_packet);
    if (av_new_packet(m_packet, encodedFrame.data.size()) < 0) {
        return;
    }
    std::memcpy(m_packet->data,
                encodedFrame.data.constData(),
                static_cast<std::size_t>(encodedFrame.data.size()));
    m_packet->pts = encodedFrame.rtpTimestamp;
    m_packet->dts = encodedFrame.rtpTimestamp;

    if (avcodec_send_packet(m_codecContext, m_packet) < 0) {
        return;
    }

    // 一次 send_packet 之后必须持续 receive_frame，直到解码器返回 EAGAIN。
    // 当前发送端关闭了 B 帧，一个 Access Unit 通常只产出一张图像，但这里仍按
    // FFmpeg 的标准调用方式取完本次能够输出的全部帧。
    while (avcodec_receive_frame(m_codecContext, m_decodedFrame) == 0) {
        // 发送端固定编码为 YUV420P，解码后继续保持同一格式，避免为了显示提前转 RGB。
        // 三个平面分别保存 Y、U、V；色度平面的宽高均为亮度平面的一半。
        if (m_decodedFrame->format != AV_PIX_FMT_YUV420P) {
            av_frame_unref(m_decodedFrame);
            continue;
        }

        const int width = m_decodedFrame->width;
        const int height = m_decodedFrame->height;
        QVideoFrame videoFrame(QVideoFrameFormat(
            QSize(width, height), QVideoFrameFormat::Format_YUV420P));
        if (!videoFrame.map(QVideoFrame::WriteOnly)) {
            av_frame_unref(m_decodedFrame);
            continue;
        }

        const int planeWidths[3] = {width, (width + 1) / 2, (width + 1) / 2};
        const int planeHeights[3] = {height, (height + 1) / 2, (height + 1) / 2};
        for (int plane = 0; plane < 3; ++plane) {
            const int bytesPerRow = std::min(planeWidths[plane],
                                             videoFrame.bytesPerLine(plane));
            for (int row = 0; row < planeHeights[plane]; ++row) {
                std::memcpy(videoFrame.bits(plane) + row * videoFrame.bytesPerLine(plane),
                            m_decodedFrame->data[plane] + row * m_decodedFrame->linesize[plane],
                            static_cast<std::size_t>(bytesPerRow));
            }
        }
        videoFrame.unmap();

        emit frameDecoded(videoFrame, encodedFrame.rtpTimestamp);
        av_frame_unref(m_decodedFrame);
    }
}
