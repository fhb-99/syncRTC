#ifndef AUDIORECEIVEPIPELINE_H
#define AUDIORECEIVEPIPELINE_H

#include <QByteArray>
#include <QObject>
#include <QtGlobal>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// 每个远端成员各自持有一个 AudioReceivePipeline，Opus 解码状态不会在成员之间混用。
class AudioReceivePipeline : public QObject
{
    Q_OBJECT
public:
    explicit AudioReceivePipeline(QObject *parent = nullptr);
    ~AudioReceivePipeline() override;

    void receiveEncodedFrame(const QByteArray &frame, quint32 rtpTimestamp);

signals:
    // 输出格式与当前采集端保持一致：48000Hz、单声道、S16 交错 PCM。
    // 本类只解码和转换采样格式，不负责 QAudioSink 播放或音画同步。
    void pcmDecoded(const QByteArray &pcmData, quint32 rtpTimestamp);

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
    struct SwrContext *m_resampleContext = nullptr;

    std::thread m_decodeThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCondition;
    std::deque<EncodedFrame> m_encodedFrames;
    bool m_stopping = false;
};

#endif // AUDIORECEIVEPIPELINE_H
