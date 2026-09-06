#include "avsyncscheduler.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QDebug>
#include <QIODevice>
#include <QMediaDevices>

#include <algorithm>

namespace {

constexpr int AudioSampleRate = 48000;
constexpr int AudioBytesPerSample = int(sizeof(qint16));
constexpr int AudioBytesPerSecond = AudioSampleRate * AudioBytesPerSample;
constexpr int AudioPrebufferBytes = AudioBytesPerSecond * 40 / 1000;
constexpr int AudioDeviceBufferBytes = AudioBytesPerSecond * 60 / 1000;
constexpr int MaxPendingAudioBytes = AudioBytesPerSecond * 120 / 1000;

constexpr quint32 VideoClockRate = 90000;
constexpr std::size_t MaxPendingVideoFrames = 3;
constexpr qint64 VideoEarlyToleranceUs = 10 * 1000;
constexpr qint64 VideoLateThresholdUs = 100 * 1000;
constexpr qint64 AudioMissingThresholdUs = 200 * 1000;

qint64 rtpDurationUs(quint32 timestampDelta, quint32 clockRate)
{
    return qint64(timestampDelta) * 1000 * 1000 / clockRate;
}

}

AvSyncScheduler::AvSyncScheduler(QObject *parent)
    : QObject(parent)
    , m_audioSink()
    , m_audioOutput(nullptr)
    , m_pendingAudio()
    , m_pendingVideoFrames()
    , m_localClock()
    , m_updateTimer()
{
    m_localClock.start();

    // 5ms 定时检查一次足以覆盖当前 30fps 视频。定时器只做少量队列判断和 PCM 写入，
    // 不执行 FFmpeg 解码，因此不会长时间占用 Qt 主线程。
    m_updateTimer.setTimerType(Qt::PreciseTimer);
    m_updateTimer.setInterval(5);
    connect(&m_updateTimer, &QTimer::timeout,
            this, &AvSyncScheduler::updatePlayback);
    m_updateTimer.start();
}

AvSyncScheduler::~AvSyncScheduler()
{
    if (m_audioSink) {
        m_audioSink->stop();
    }
}

void AvSyncScheduler::queueVideoFrame(const QVideoFrame &frame, quint32 rtpTimestamp)
{
    if (!m_hasVideoTimestamp) {
        // 音频和视频 RTP 时间戳使用不同的时钟频率，起点也不保证相同。
        // 因此分别记录首帧时间戳和本地到达时刻，后面只使用各自的相对增量。
        m_firstVideoTimestamp = rtpTimestamp;
        m_firstVideoArrivalUs = currentLocalTimeUs();
        m_hasVideoTimestamp = true;
    }

    // 解码后的视频最多保留三帧。同步线程来不及显示时丢掉最旧画面，
    // 避免会议画面虽然连续却一直播放数秒以前的内容。
    if (m_pendingVideoFrames.size() >= MaxPendingVideoFrames) {
        m_pendingVideoFrames.pop_front();
    }
    m_pendingVideoFrames.push_back(PendingVideoFrame{frame, rtpTimestamp});
}

void AvSyncScheduler::queueAudioPcm(const QByteArray &pcmData, quint32 rtpTimestamp)
{
    if (m_audioPlaybackUnavailable) {
        return;
    }

    if (!m_hasAudioTimestamp) {
        m_firstAudioTimestamp = rtpTimestamp;
        m_firstAudioArrivalUs = currentLocalTimeUs();
        m_hasAudioTimestamp = true;
    }
    m_lastAudioArrivalUs = currentLocalTimeUs();

    // 解码器输出固定为 48000Hz、单声道、S16。先预缓冲约 40ms，减少系统线程
    // 短暂调度抖动造成的立即欠载；尚未写入声卡的数据最多保留约 120ms。
    m_pendingAudio.append(pcmData);
    if (m_pendingAudio.size() > MaxPendingAudioBytes) {
        m_pendingAudio.remove(0, m_pendingAudio.size() - MaxPendingAudioBytes);
    }

    if (!m_audioSink && m_pendingAudio.size() >= AudioPrebufferBytes) {
        startAudioPlayback();
    }
    writePendingAudio();
}

void AvSyncScheduler::startAudioPlayback()
{
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        qWarning() << "No audio output device available";
        m_audioPlaybackUnavailable = true;
        m_pendingAudio.clear();
        return;
    }

    QAudioFormat format;
    format.setSampleRate(AudioSampleRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(format)) {
        qWarning() << "Audio output does not support 48000Hz mono Int16 PCM";
        m_audioPlaybackUnavailable = true;
        m_pendingAudio.clear();
        return;
    }

    // 每个 RemoteMediaReceiver 拥有自己的 QAudioSink，多个远端成员由操作系统混音。
    // setBufferSize 必须在 start() 之前调用；60ms 既能容纳预缓冲，也不会引入明显延迟。
    m_audioSink = std::make_unique<QAudioSink>(device, format);
    m_audioSink->setBufferSize(AudioDeviceBufferBytes);
    m_audioOutput = m_audioSink->start();
    if (!m_audioOutput) {
        qWarning() << "Audio output failed to start";
        m_audioSink.reset();
        m_audioPlaybackUnavailable = true;
        m_pendingAudio.clear();
    }
}

void AvSyncScheduler::writePendingAudio()
{
    if (!m_audioSink || !m_audioOutput || m_pendingAudio.isEmpty()) {
        return;
    }

    // QAudioSink 的内部缓冲区容量有限，每次只写 bytesFree() 能接收的部分。
    // 未写完的 PCM 留在 m_pendingAudio，下一次音频到达或定时器触发时继续写入。
    const qsizetype bytesToWrite = std::min(m_audioSink->bytesFree(),
                                            m_pendingAudio.size());
    if (bytesToWrite <= 0) {
        return;
    }

    const qint64 bytesWritten = m_audioOutput->write(m_pendingAudio.constData(),
                                                     bytesToWrite);
    if (bytesWritten > 0) {
        m_pendingAudio.remove(0, bytesWritten);
    }
}

void AvSyncScheduler::updatePlayback()
{
    writePendingAudio();

    if (m_pendingVideoFrames.empty()) {
        return;
    }

    // 已收到音频但尚未攒够 40ms 时先等待，确保第一张视频帧和第一段真正播放的
    // 音频从同一条时间线开始。没有音频轨道时则直接使用本地单调时钟播放视频。
    if (m_hasAudioTimestamp
        && !m_audioPlaybackUnavailable
        && !m_audioSink
        && currentLocalTimeUs() - m_lastAudioArrivalUs < AudioMissingThresholdUs) {
        return;
    }

    while (!m_pendingVideoFrames.empty()) {
        const PendingVideoFrame &videoFrame = m_pendingVideoFrames.front();
        const qint64 masterClock = audioClockUs();
        const qint64 videoTime = videoPresentationTimeUs(videoFrame.rtpTimestamp);
        const qint64 difference = videoTime - masterClock;

        if (difference > VideoEarlyToleranceUs) {
            // 视频比音频早到，继续留在队列中，等主时钟追上它后再显示。
            return;
        }

        if (difference < -VideoLateThresholdUs) {
            // 视频已经落后音频超过 100ms，继续显示只会扩大观感延迟，直接丢弃并检查下一帧。
            m_pendingVideoFrames.pop_front();
            continue;
        }

        // 允许视频最多提前约 10ms 提交，避免 5ms 定时检查造成不必要的下一轮等待。
        // 此处发出的帧已经完成同步判断，上层不应再次按照原始 RTP 时间戳等待。
        emit videoFrameReady(videoFrame.frame);
        m_pendingVideoFrames.pop_front();
        return;
    }
}

qint64 AvSyncScheduler::currentLocalTimeUs() const
{
    return m_localClock.nsecsElapsed() / 1000;
}

qint64 AvSyncScheduler::audioClockUs() const
{
    if (usesAudioClock()) {
        // 音频 RTP 时钟固定为 48000Hz。首帧时间戳确定媒体时间起点，
        // processedUSecs() 表示声卡已经实际消费的时长，两者相加就是音频主时钟。
        const qint64 firstAudioTimeUs = rtpDurationUs(m_firstAudioTimestamp,
                                                      AudioSampleRate);
        return firstAudioTimeUs + m_audioSink->processedUSecs();
    }

    // 远端成员没有发送音频，或者本机没有可用输出设备时，用单调时钟作为视频外部时钟。
    return currentLocalTimeUs();
}

bool AvSyncScheduler::usesAudioClock() const
{
    if (!m_audioSink) {
        return false;
    }

    // 对端关闭麦克风或音频暂时断流后，QAudioSink 会在缓冲区耗尽时进入 IdleState，
    // processedUSecs() 也不再前进。此时继续把它当主时钟会让视频永久停住，
    // 所以在 200ms 内没有新 PCM 且待写队列为空时临时退回本地单调时钟。
    const bool audioRecentlyArrived = currentLocalTimeUs() - m_lastAudioArrivalUs
                                      < AudioMissingThresholdUs;
    return m_audioSink->state() != QAudio::IdleState
           || audioRecentlyArrived
           || !m_pendingAudio.isEmpty();
}

qint64 AvSyncScheduler::videoPresentationTimeUs(quint32 rtpTimestamp) const
{
    // quint32 减法会自然处理 RTP 时间戳回绕，结果表示相对于第一帧的媒体时长。
    const quint32 timestampDelta = rtpTimestamp - m_firstVideoTimestamp;
    const qint64 videoElapsedUs = rtpDurationUs(timestampDelta, VideoClockRate);

    if (usesAudioClock()) {
        // 音视频 RTP 起点彼此独立，不能直接比较两个原始时间戳。
        // 这里把“首帧本地到达时间差”作为两条媒体时间线的近似偏移，再把视频
        // 相对时长映射到音频媒体时间轴。后续具备 RTCP SR 后才能替换成标准 NTP 映射。
        const qint64 firstAudioTimeUs = rtpDurationUs(m_firstAudioTimestamp,
                                                      AudioSampleRate);
        const qint64 firstFrameArrivalDifference = m_firstVideoArrivalUs
                                                   - m_firstAudioArrivalUs;
        return firstAudioTimeUs + firstFrameArrivalDifference + videoElapsedUs;
    }

    return m_firstVideoArrivalUs + videoElapsedUs;
}
