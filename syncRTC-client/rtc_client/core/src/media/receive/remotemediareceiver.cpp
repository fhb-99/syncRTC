#include "remotemediareceiver.h"

RemoteMediaReceiver::RemoteMediaReceiver(int publisherUid, QObject *parent)
    : QObject(parent)
    , m_publisherUid(publisherUid)
    , m_videoPipeline()
    , m_audioPipeline()
    , m_syncScheduler()
{
    // 两个 Pipeline 在各自解码线程中发出信号，AvSyncScheduler 位于 Qt 主线程。
    // Qt 自动使用队列连接，因此解码线程只交出结果，不会直接操作 QAudioSink 或定时器。
    connect(&m_videoPipeline, &VideoReceivePipeline::frameDecoded,
            &m_syncScheduler, &AvSyncScheduler::queueVideoFrame);
    connect(&m_audioPipeline, &AudioReceivePipeline::pcmDecoded,
            &m_syncScheduler, &AvSyncScheduler::queueAudioPcm);

    // Scheduler 只在视频到达显示时间后发出信号，这里补回发布者 UID，
    // 让 MediaController 能把画面交给对应成员的 QVideoSink。
    connect(&m_syncScheduler, &AvSyncScheduler::videoFrameReady,
            this, [this](const QVideoFrame &frame) {
        emit videoFrameReady(m_publisherUid, frame);
    });
}

RemoteMediaReceiver::~RemoteMediaReceiver() = default;

void RemoteMediaReceiver::receiveVideoFrame(const QByteArray &frame, quint32 rtpTimestamp)
{
    // 远端视频和音频在这里分流。视频数据保持 H.264 编码格式，交给视频接收管线。
    m_videoPipeline.receiveEncodedFrame(frame, rtpTimestamp);
}

void RemoteMediaReceiver::receiveAudioFrame(const QByteArray &frame, quint32 rtpTimestamp)
{
    // Opus 编码帧只进入音频接收管线，不在协调层修改内容或时间戳。
    m_audioPipeline.receiveEncodedFrame(frame, rtpTimestamp);
}
