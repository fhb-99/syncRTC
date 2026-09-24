#ifndef VIDEOENCODEWORKER_H
#define VIDEOENCODEWORKER_H

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>
#include <QVector>

#include <atomic>
#include <memory>
#include <mutex>

#include "../models/global.h"

#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>

// 视频采集帧从 Qt 主线程进入这里后，仅保留最新的一帧等待编码。
// 会议视频更关注实时画面，编码来不及时直接丢弃旧帧，不能让队列不断积累历史画面。
class VideoEncodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit VideoEncodeWorker(QObject *parent = nullptr);
    ~VideoEncodeWorker() override;

    // 此函数由采集所在的主线程调用，内部只做一次帧引用替换和唤醒，不执行转换或编码。
    void enqueueVideoFrame(const QVideoFrame &frame);
    // 由 MediaController 在线程启动前设置。控制器会先停掉视频 worker，再停止 RTC 线程，
    // 因此这里仅保存非 owning 指针即可完成单向 RTP 提交。
    void setTransportWorker(class RtcTransportWorker *transportWorker);

public slots:
    // 以下函数只在视频工作线程执行，FFmpeg 编码器和 RTP 打包器不会被多个线程同时访问。
    void start();
    void stop();

private slots:
    void processNextFrame();

private:
    QVector<QByteArray> packetizeH264Frame(const QByteArray &h264Frame);
    void processVideoFrame(const QVideoFrame &frame);
    void sendVideoFrame(AVFrame *frame);

    AVCodecContext *m_videoCodecCtx = nullptr;
    AVPacket *m_videoPacket = nullptr;
    AVFrame *m_videoFrame = nullptr;
    SwsContext *m_swsContext = nullptr;
    bool m_videoStarted = false;
    int64_t m_videoPts = 0;
    quint32 m_videoTimestamp = 0;

    std::shared_ptr<rtc::RtpPacketizationConfig> m_videoRtpConfig;
    std::shared_ptr<rtc::H264RtpPacketizer> m_videoPacketizer;
    class RtcTransportWorker *m_transportWorker = nullptr;

    // 采集线程与工作线程之间只有这个单帧槽共享。m_frameScheduled 用于保证同一时刻
    // 只有一个 processNextFrame 事件排在工作线程事件队列中，从而避免 Qt 事件无限增长。
    std::mutex m_frameMutex;
    QVideoFrame m_pendingFrame;
    bool m_frameScheduled = false;
    std::atomic_bool m_acceptingFrames = false;
};

#endif // VIDEOENCODEWORKER_H
