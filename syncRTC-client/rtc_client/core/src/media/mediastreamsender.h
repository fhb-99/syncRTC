#ifndef MEDIASTREAMSENDER_H
#define MEDIASTREAMSENDER_H

#include <QObject>

class MediaStreamSender : public QObject
{
    Q_OBJECT
public:
    explicit MediaStreamSender(QObject *parent = nullptr);
    ~MediaStreamSender() override;

    // 后续负责接收采集帧，并完成编码、RTP 封装和媒体发送。
    void startVideo();
    void stopVideo();
    void startAudio();
    void stopAudio();
    void stopAll();
};

#endif // MEDIASTREAMSENDER_H
