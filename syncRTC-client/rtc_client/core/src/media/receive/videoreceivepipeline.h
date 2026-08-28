#ifndef VIDEORECEIVEPIPELINE_H
#define VIDEORECEIVEPIPELINE_H

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>
#include <QtGlobal>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// 每个远端成员各自持有一个 VideoReceivePipeline，因此每路 H.264 视频都有独立的
// FFmpeg 解码上下文，不会混用 SPS、PPS、分辨率和参考帧状态。
class VideoReceivePipeline : public QObject
{
    Q_OBJECT
public:
    explicit VideoReceivePipeline(QObject *parent = nullptr);
    ~VideoReceivePipeline() override;

    void receiveEncodedFrame(const QByteArray &frame, quint32 rtpTimestamp);

signals:
    // 解码线程只负责产出可供 Qt 使用的 YUV420P 视频帧，不在这里决定显示时机。
    // 后续渲染或音画同步模块通过队列连接接收该信号，不会阻塞当前解码线程。
    void frameDecoded(const QVideoFrame &frame, quint32 rtpTimestamp);

private:
    struct EncodedFrame
    {
        QByteArray data;
        quint32 rtpTimestamp = 0;
    };

    void decodeLoop();
    void decodeFrame(const EncodedFrame &encodedFrame);

    struct AVCodecContext *m_codecContext = nullptr;
    struct AVPacket *m_packet = nullptr;
    struct AVFrame *m_decodedFrame = nullptr;

    std::thread m_decodeThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::deque<EncodedFrame> m_encodedFrames;
    bool m_stopping = false;
};

#endif // VIDEORECEIVEPIPELINE_H
