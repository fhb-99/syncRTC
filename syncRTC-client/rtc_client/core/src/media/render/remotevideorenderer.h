#ifndef REMOTEVIDEORENDERER_H
#define REMOTEVIDEORENDERER_H

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QVideoFrame>

class QVideoSink;

// 保存“成员 UID -> QML VideoOutput.videoSink”的对应关系。
// MediaController 只把同步完成的视频帧交给本类，不需要知道 QML 中具体使用了
// Repeater、GridView 还是主讲人布局；界面销毁某个 VideoOutput 时解除对应关系即可。
class RemoteVideoRenderer : public QObject
{
    Q_OBJECT
public:
    explicit RemoteVideoRenderer(QObject *parent = nullptr);

    void bindLocalVideoSink(QVideoSink *sink);
    void unbindLocalVideoSink(QVideoSink *sink);
    void bindRemoteVideoSink(int publisherUid, QVideoSink *sink);
    void unbindRemoteVideoSink(int publisherUid, QVideoSink *sink);

    void renderLocalFrame(const QVideoFrame &frame);
    void renderRemoteFrame(int publisherUid, const QVideoFrame &frame);
    void clearLocalFrame();
    void clearFrames();
    bool localVideoAvailable() const;

signals:
    void localVideoAvailableChanged();

private:
    QPointer<QVideoSink> m_localVideoSink;
    QVideoFrame m_latestLocalFrame;
    QHash<int, QPointer<QVideoSink>> m_remoteVideoSinks;
    QHash<int, QVideoFrame> m_latestRemoteFrames;
    bool m_localVideoAvailable = false;
};

#endif // REMOTEVIDEORENDERER_H
