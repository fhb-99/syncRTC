#include "audioreceivepipeline.h"

#include "../../models/global.h"

#include <cstring>

namespace {

constexpr int AudioSampleRate = 48000;
constexpr int AudioChannelCount = 1;
constexpr std::size_t MaxEncodedAudioFrames = 3;

}

AudioReceivePipeline::AudioReceivePipeline(QObject *parent)
    : QObject(parent)
{
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
    m_codecContext = avcodec_alloc_context3(codec);
    m_codecContext->sample_rate = AudioSampleRate;
    av_channel_layout_default(&m_codecContext->ch_layout, AudioChannelCount);
    m_codecContext->pkt_timebase = AVRational{1, AudioSampleRate};
    avcodec_open2(m_codecContext, codec, nullptr);

    m_packet = av_packet_alloc();
    m_decodedFrame = av_frame_alloc();

    // Opus 解码和采样格式转换在独立线程完成，接收线程只做一次 QByteArray 拷贝和入队。
    m_decodeThread = std::thread(&AudioReceivePipeline::decodeLoop, this);
}

AudioReceivePipeline::~AudioReceivePipeline()
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_stopping = true;
    }
    m_queueCondition.notify_one();

    if (m_decodeThread.joinable()) {
        m_decodeThread.join();
    }

    swr_free(&m_resampleContext);
    av_frame_free(&m_decodedFrame);
    av_packet_free(&m_packet);
    avcodec_free_context(&m_codecContext);
}

void AudioReceivePipeline::receiveEncodedFrame(const QByteArray &frame, quint32 rtpTimestamp)
{
    // OpusRtpDepacketizer 已去掉 RTP 头，frame 是单个完整的 Opus 编码帧。
    // 三帧对应当前 20ms 编码配置下约 60ms 音频，既给解码线程留出短暂调度余量，
    // 又避免程序卡顿后继续播放很久以前的声音。
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        if (m_encodedFrames.size() >= MaxEncodedAudioFrames) {
            m_encodedFrames.pop_front();
        }
        m_encodedFrames.push_back(EncodedFrame{frame, rtpTimestamp});
    }
    m_queueCondition.notify_one();
}

void AudioReceivePipeline::decodeLoop()
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

void AudioReceivePipeline::decodeFrame(const EncodedFrame &encodedFrame)
{
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

    while (avcodec_receive_frame(m_codecContext, m_decodedFrame) == 0) {
        if (!m_resampleContext) {
            // Opus 解码器通常输出 FLTP。播放侧使用当前项目采集端同样的
            // 48000Hz / 单声道 / S16，因此在首个解码帧到达时创建一次 SwrContext。
            AVChannelLayout outputLayout;
            av_channel_layout_default(&outputLayout, AudioChannelCount);
            swr_alloc_set_opts2(&m_resampleContext,
                                &outputLayout,
                                AV_SAMPLE_FMT_S16,
                                AudioSampleRate,
                                &m_decodedFrame->ch_layout,
                                static_cast<AVSampleFormat>(m_decodedFrame->format),
                                m_decodedFrame->sample_rate,
                                0,
                                nullptr);
            av_channel_layout_uninit(&outputLayout);
            swr_init(m_resampleContext);
        }

        // swr_get_delay 把重采样器内部尚未输出的采样也计入缓冲区大小，避免截断 PCM。
        const int outputSampleCount = static_cast<int>(av_rescale_rnd(
            swr_get_delay(m_resampleContext, m_decodedFrame->sample_rate)
                + m_decodedFrame->nb_samples,
            AudioSampleRate,
            m_decodedFrame->sample_rate,
            AV_ROUND_UP));
        QByteArray pcmData(outputSampleCount * int(sizeof(qint16)), '\0');
        uint8_t *outputData[] = {
            reinterpret_cast<uint8_t *>(pcmData.data())
        };

        const int convertedSamples = swr_convert(
            m_resampleContext,
            outputData,
            outputSampleCount,
            const_cast<const uint8_t **>(m_decodedFrame->extended_data),
            m_decodedFrame->nb_samples);
        if (convertedSamples > 0) {
            pcmData.resize(convertedSamples * int(sizeof(qint16)));
            emit pcmDecoded(pcmData, encodedFrame.rtpTimestamp);
        }

        av_frame_unref(m_decodedFrame);
    }
}
