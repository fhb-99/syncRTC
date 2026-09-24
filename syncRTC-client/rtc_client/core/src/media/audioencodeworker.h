#ifndef AUDIOENCODEWORKER_H
#define AUDIOENCODEWORKER_H

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <memory>
#include <mutex>

#include "../models/global.h"

#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/rtppacketizer.hpp>

// 麦克风 PCM 从主线程进入这里后由独立线程完成分帧、Opus 编码和 RTP 封装。
// 队列只保留很短的实时音频，避免主线程或编码线程短暂繁忙时把语音积压成历史声音。
class AudioEncodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit AudioEncodeWorker(QObject *parent = nullptr);
    ~AudioEncodeWorker() override;

    // 由麦克风采集所在的主线程调用；此处只负责有界入队和唤醒，不做 PCM 转换或编码。
    void enqueueAudioPcmData(const QByteArray &pcmData);
    // 由 MediaController 在线程启动前设置。控制器会先停掉本 worker，再停止 RTC 线程，
    // 因此编码期间持有的裸指针始终有效，无需为这一条单向提交链路引入额外所有权。
    void setTransportWorker(class RtcTransportWorker *transportWorker);

public slots:
    // 以下函数仅在音频工作线程执行，Opus 上下文和 RTP 打包器不会被多个线程同时访问。
    void start();
    void stop();

private slots:
    void processPendingPcm();

private:
    void encodeAudioFrame(const QByteArray &pcmFrame);

    AVCodecContext *m_audioCodecCtx = nullptr;
    AVPacket *m_audioPacket = nullptr;
    AVFrame *m_audioFrame = nullptr;
    bool m_audioStarted = false;
    int64_t m_audioPts = 0;
    quint32 m_audioTimestamp = 0;
    QByteArray m_audioPcmBuffer;
    std::shared_ptr<rtc::RtpPacketizationConfig> m_audioRtpConfig;
    std::shared_ptr<rtc::OpusRtpPacketizer> m_audioPacketizer;
    class RtcTransportWorker *m_transportWorker = nullptr;

    // 音频输入上限为 3 个 20ms 帧。满时丢弃最早的 PCM，以有限丢帧换取有限端到端时延。
    std::mutex m_pcmMutex;
    QByteArray m_pendingPcm;
    quint64 m_droppedSamples = 0;
    bool m_processScheduled = false;
    std::atomic_bool m_acceptingPcm = false;
};

#endif // AUDIOENCODEWORKER_H
