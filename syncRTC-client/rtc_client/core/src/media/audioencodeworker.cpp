#include "audioencodeworker.h"

#include "rtctransportworker.h"

#include <QMetaObject>

namespace {

constexpr int AudioSampleRate = 48000;
constexpr int AudioFrameSamples = AudioSampleRate * 20 / 1000;
constexpr int AudioFrameBytes = AudioFrameSamples * int(sizeof(qint16));
constexpr int MaxQueuedAudioBytes = AudioFrameBytes * 3;

}

AudioEncodeWorker::AudioEncodeWorker(QObject *parent)
    : QObject(parent)
{
}

AudioEncodeWorker::~AudioEncodeWorker()
{
    stop();
}

void AudioEncodeWorker::setTransportWorker(RtcTransportWorker *transportWorker)
{
    m_transportWorker = transportWorker;
}

void AudioEncodeWorker::enqueueAudioPcmData(const QByteArray &pcmData)
{
    if (!m_acceptingPcm.load() || pcmData.isEmpty()) {
        return;
    }

    bool shouldSchedule = false;
    {
        std::lock_guard<std::mutex> lock(m_pcmMutex);
        m_pendingPcm.append(pcmData);

        // PCM 是 16bit 单声道，始终按完整采样点丢弃，不能截断半个采样点。
        const int overflowBytes = m_pendingPcm.size() - MaxQueuedAudioBytes;
        const int droppedBytes = overflowBytes > 0
            ? overflowBytes - overflowBytes % int(sizeof(qint16))
            : 0;
        if (droppedBytes > 0) {
            m_pendingPcm.remove(0, droppedBytes);
            m_droppedSamples += static_cast<quint64>(droppedBytes / int(sizeof(qint16)));
        }

        // 同一时刻最多只投递一个处理事件；新增 PCM 由该事件一次取走，避免事件队列持续增长。
        if (!m_processScheduled) {
            m_processScheduled = true;
            shouldSchedule = true;
        }
    }

    if (shouldSchedule) {
        QMetaObject::invokeMethod(this, &AudioEncodeWorker::processPendingPcm,
                                  Qt::QueuedConnection);
    }
}

void AudioEncodeWorker::start()
{
    if (m_audioStarted) {
        return;
    }

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
    m_audioCodecCtx = avcodec_alloc_context3(codec);
    m_audioCodecCtx->sample_rate = AudioSampleRate;
    m_audioCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecCtx->bit_rate = 32000;
    m_audioCodecCtx->time_base = AVRational{1, AudioSampleRate};
    av_channel_layout_default(&m_audioCodecCtx->ch_layout, 1);
    avcodec_open2(m_audioCodecCtx, codec, nullptr);

    m_audioPacket = av_packet_alloc();
    m_audioFrame = av_frame_alloc();
    m_audioFrame->nb_samples = AudioFrameSamples;
    m_audioFrame->format = m_audioCodecCtx->sample_fmt;
    m_audioFrame->sample_rate = m_audioCodecCtx->sample_rate;
    av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCodecCtx->ch_layout);
    av_frame_get_buffer(m_audioFrame, 0);

    const uint32_t audioSsrc = 654321;
    const uint8_t payloadType = 111;
    const std::string cname = "audio";
    m_audioRtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
        audioSsrc, cname, payloadType, rtc::OpusRtpPacketizer::DefaultClockRate);
    m_audioPacketizer = std::make_shared<rtc::OpusRtpPacketizer>(m_audioRtpConfig);

    m_audioPts = 0;
    m_audioTimestamp = 0;
    m_audioPcmBuffer.clear();
    m_audioStarted = true;
    m_acceptingPcm.store(true);
}

void AudioEncodeWorker::stop()
{
    m_acceptingPcm.store(false);
    {
        std::lock_guard<std::mutex> lock(m_pcmMutex);
        m_pendingPcm.clear();
        m_droppedSamples = 0;
        m_processScheduled = false;
    }

    if (!m_audioStarted) {
        return;
    }

    // 这些对象始终由音频线程创建、使用和释放，不能在主线程关闭麦克风时直接释放。
    av_frame_free(&m_audioFrame);
    av_packet_free(&m_audioPacket);
    avcodec_free_context(&m_audioCodecCtx);
    m_audioPacketizer.reset();
    m_audioRtpConfig.reset();

    m_audioPts = 0;
    m_audioTimestamp = 0;
    m_audioPcmBuffer.clear();
    m_audioStarted = false;
}

void AudioEncodeWorker::processPendingPcm()
{
    QByteArray pendingPcm;
    quint64 droppedSamples = 0;
    {
        std::lock_guard<std::mutex> lock(m_pcmMutex);
        m_processScheduled = false;
        if (!m_acceptingPcm.load()) {
            return;
        }
        pendingPcm = std::move(m_pendingPcm);
        droppedSamples = m_droppedSamples;
        m_droppedSamples = 0;
    }

    // 丢掉积压 PCM 后需要同步推进时间轴，不能把后续语音伪装成连续的旧时间片。
    m_audioPts += static_cast<int64_t>(droppedSamples);
    m_audioTimestamp += static_cast<quint32>(droppedSamples);
    m_audioPcmBuffer.append(pendingPcm);

    while (m_audioPcmBuffer.size() >= AudioFrameBytes) {
        const QByteArray pcmFrame = m_audioPcmBuffer.left(AudioFrameBytes);
        m_audioPcmBuffer.remove(0, AudioFrameBytes);
        encodeAudioFrame(pcmFrame);
    }
}

void AudioEncodeWorker::encodeAudioFrame(const QByteArray &pcmFrame)
{
    if (!m_audioStarted || av_frame_make_writable(m_audioFrame) < 0) {
        return;
    }

    // 采集侧为 Int16 PCM，FFmpeg 原生 Opus 编码器要求 FLTP，因此逐采样点转换。
    const qint16 *src = reinterpret_cast<const qint16 *>(pcmFrame.constData());
    float *dst = reinterpret_cast<float *>(m_audioFrame->data[0]);
    for (int i = 0; i < AudioFrameSamples; ++i) {
        dst[i] = float(src[i]) / 32768.0f;
    }

    m_audioFrame->pts = m_audioPts;
    m_audioPts += m_audioFrame->nb_samples;
    avcodec_send_frame(m_audioCodecCtx, m_audioFrame);

    while (avcodec_receive_packet(m_audioCodecCtx, m_audioPacket) == 0) {
        rtc::binary sample(
            reinterpret_cast<const rtc::byte *>(m_audioPacket->data),
            reinterpret_cast<const rtc::byte *>(m_audioPacket->data + m_audioPacket->size)
            );
        auto frameInfo = std::make_shared<rtc::FrameInfo>(m_audioTimestamp);
        rtc::message_vector messages;
        messages.push_back(rtc::make_message(sample.begin(), sample.end(), frameInfo));
        m_audioPacketizer->outgoing(messages, [](rtc::message_ptr) {});

        for (const rtc::message_ptr &message : messages) {
            // 编码线程到此为止只负责得到 RTP 包。交给 RTC 传输线程后，由它串行调用
            // Track::send()，避免音频编码线程和视频编码线程并发触碰同一个 PeerConnection。
            m_transportWorker->enqueueAudioRtpPacket(QByteArray(
                reinterpret_cast<const char *>(message->data()), int(message->size())));
        }
        av_packet_unref(m_audioPacket);
    }

    // 48kHz 下 20ms 正好为 960 个采样点。
    m_audioTimestamp += AudioFrameSamples;
}
