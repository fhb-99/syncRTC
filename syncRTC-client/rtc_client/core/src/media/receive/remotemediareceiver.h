#ifndef REMOTEMEDIARECEIVER_H
#define REMOTEMEDIARECEIVER_H

#include "audioreceivepipeline.h"
#include "avsyncscheduler.h"
#include "videoreceivepipeline.h"

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>
#include <QtGlobal>

// 管理一个远端参会者的音视频接收链路。
// MediaController 按发布者 UID 为每个远端成员持有一个本类实例。
class RemoteMediaReceiver : public QObject
{
    Q_OBJECT
public:
    explicit RemoteMediaReceiver(int publisherUid, QObject *parent = nullptr);
    ~RemoteMediaReceiver() override;

    void receiveVideoFrame(const QByteArray &frame, quint32 rtpTimestamp);
    void receiveAudioFrame(const QByteArray &frame, quint32 rtpTimestamp);

signals:
    // 只有经过音频主时钟调度、已经到达显示时间的视频帧才会交回 MediaController。
    // 音频 PCM 由 AvSyncScheduler 内部的 QAudioSink 播放，不再绕回 Controller。
    void videoFrameReady(int publisherUid, const QVideoFrame &frame);

private:
    int m_publisherUid = 0;
    VideoReceivePipeline m_videoPipeline;
    AudioReceivePipeline m_audioPipeline;
    AvSyncScheduler m_syncScheduler;
};

#endif // REMOTEMEDIARECEIVER_H
