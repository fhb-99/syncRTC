#ifndef MEDIASTREAMPROCESSOR_H
#define MEDIASTREAMPROCESSOR_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QVideoFrame>

#include <memory>

#include <QtGlobal>
#include "../models/global.h"

#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>
#include <rtc/message.hpp>
#include <rtc/frameinfo.hpp>

class MediaStreamProcessor : public QObject
{
    Q_OBJECT
public:
    explicit MediaStreamProcessor(QObject *parent = nullptr);
    ~MediaStreamProcessor() override;

    QVector<QByteArray> packetizeH264Frame(const QByteArray &h264Frame);

    void processVideoFrame(const QVideoFrame &frame);
    void processVideoFrame(AVFrame * frame);
    void processAudioPcmData(const QByteArray &pcmData);

    // 初始化编码器
    void startVideo();
    void stopVideo();
    void startAudio();
    void stopAudio();
    void stopAll();

private:
    // 视频
    AVCodecContext *m_videoCodecCtx;
    AVPacket *m_videoPacket;
    bool m_videoStarted = false;
    int64_t m_videoPts = 0;
    quint16 m_videoSequence = 0;
    quint32 m_videoTimestamp = 0;


    // 封装函数相关
    std::shared_ptr<rtc::RtpPacketizationConfig> m_videoRtpConfig;
    std::shared_ptr<rtc::H264RtpPacketizer> m_videoPacketizer;

    // 音频
    AVCodecContext *m_audioCodecCtx;
    AVPacket *m_audioPacket;
    bool m_audioStarted = false;
    int64_t m_audioPts = 0;
    quint32 m_audioTimestamp = 0;

    // 暂存麦克风采集到的 PCM 原始数据，后续 Opus 编码会从这里按固定时长取走。
    QByteArray m_audioPcmBuffer;
    std::shared_ptr<rtc::RtpPacketizationConfig> m_audioRtpConfig;
    std::shared_ptr<rtc::OpusRtpPacketizer> m_audioPacketizer;
};

#endif // MEDIASTREAMPROCESSOR_H
