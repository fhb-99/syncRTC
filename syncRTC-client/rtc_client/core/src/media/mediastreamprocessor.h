#ifndef MEDIASTREAMPROCESSOR_H
#define MEDIASTREAMPROCESSOR_H

#include <QObject>

#include <QtGlobal>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/pixfmt.h>
}

class MediaStreamProcessor : public QObject
{
    Q_OBJECT
public:
    explicit MediaStreamProcessor(QObject *parent = nullptr);
    ~MediaStreamProcessor() override;

    void processVideoFrame(AVFrame * frame);

    // 初始化编码器
    void startVideo();
    void stopVideo();
    void startAudio();
    void stopAudio();
    void stopAll();

private:
    AVCodecContext *m_videoCodecCtx;
    AVPacket *m_videoPacket;
    bool m_videoStarted = false;
    int64_t m_videoPts = 0;
    quint16 m_videoSequence = 0;
    quint32 m_videoTimestamp = 0;
};

#endif // MEDIASTREAMPROCESSOR_H
