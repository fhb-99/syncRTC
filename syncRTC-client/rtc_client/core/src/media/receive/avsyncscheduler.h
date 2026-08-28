#ifndef AVSYNCSCHEDULER_H
#define AVSYNCSCHEDULER_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QVideoFrame>
#include <QtGlobal>

#include <deque>
#include <memory>

class QAudioSink;
class QIODevice;

// 管理同一个远端成员的音频播放和视频显示节奏。
// 音频由声卡连续播放，因此使用 QAudioSink 已实际处理的时长作为主时钟；
// 视频帧只负责等待、按时显示或在严重落后时丢弃，不反过来改变音频播放速度。
class AvSyncScheduler : public QObject
{
    Q_OBJECT
public:
    explicit AvSyncScheduler(QObject *parent = nullptr);
    ~AvSyncScheduler() override;

    void queueVideoFrame(const QVideoFrame &frame, quint32 rtpTimestamp);
    void queueAudioPcm(const QByteArray &pcmData, quint32 rtpTimestamp);

signals:
    // 该信号发出时，视频帧已经根据音频主时钟完成等待和过期判断，
    // 上层只需要把它提交给对应成员的 QVideoSink。
    void videoFrameReady(const QVideoFrame &frame);

private:
    struct PendingVideoFrame
    {
        QVideoFrame frame;
        quint32 rtpTimestamp = 0;
    };

    void startAudioPlayback();
    void writePendingAudio();
    void updatePlayback();
    qint64 currentLocalTimeUs() const;
    bool usesAudioClock() const;
    qint64 audioClockUs() const;
    qint64 videoPresentationTimeUs(quint32 rtpTimestamp) const;

    std::unique_ptr<QAudioSink> m_audioSink;
    QIODevice *m_audioOutput = nullptr;
    QByteArray m_pendingAudio;
    std::deque<PendingVideoFrame> m_pendingVideoFrames;

    QElapsedTimer m_localClock;
    QTimer m_updateTimer;

    quint32 m_firstAudioTimestamp = 0;
    quint32 m_firstVideoTimestamp = 0;
    qint64 m_firstAudioArrivalUs = 0;
    qint64 m_firstVideoArrivalUs = 0;
    qint64 m_lastAudioArrivalUs = 0;
    bool m_hasAudioTimestamp = false;
    bool m_hasVideoTimestamp = false;
    bool m_audioPlaybackUnavailable = false;
};

#endif // AVSYNCSCHEDULER_H
