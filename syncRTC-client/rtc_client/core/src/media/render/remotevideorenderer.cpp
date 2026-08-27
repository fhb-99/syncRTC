#include "remotevideorenderer.h"

#include <QVideoSink>

#include <utility>

RemoteVideoRenderer::RemoteVideoRenderer(QObject *parent)
    : QObject(parent)
    , m_localVideoSink(nullptr)
    , m_latestLocalFrame()
    , m_remoteVideoSinks()
    , m_latestRemoteFrames()
{
}

void RemoteVideoRenderer::bindLocalVideoSink(QVideoSink *sink)
{
    m_localVideoSink = sink;
    if (m_localVideoSink && m_latestLocalFrame.isValid()) {
        // 摄像头帧可能早于 QML 页面创建。绑定时补交最后一帧，避免 VideoOutput
        // 必须等到下一次摄像头回调才能出现本地预览。
        m_localVideoSink->setVideoFrame(m_latestLocalFrame);
    }
}

void RemoteVideoRenderer::unbindLocalVideoSink(QVideoSink *sink)
{
    if (m_localVideoSink == sink) {
        m_localVideoSink.clear();
    }
}

void RemoteVideoRenderer::bindRemoteVideoSink(int publisherUid, QVideoSink *sink)
{
    m_remoteVideoSinks.insert(publisherUid, sink);

    const auto frame = m_latestRemoteFrames.constFind(publisherUid);
    if (sink && frame != m_latestRemoteFrames.cend()) {
        // 成员数据和媒体 Track 的到达顺序不固定。若视频帧先到，QML 创建该成员区域后
        // 立即显示缓存的最后一帧，不需要为此修改解码或同步链路。
        sink->setVideoFrame(frame.value());
    }
}

void RemoteVideoRenderer::unbindRemoteVideoSink(int publisherUid, QVideoSink *sink)
{
    const auto currentSink = m_remoteVideoSinks.constFind(publisherUid);
    if (currentSink != m_remoteVideoSinks.cend() && currentSink.value() == sink) {
        m_remoteVideoSinks.remove(publisherUid);
        m_latestRemoteFrames.remove(publisherUid);
    }
}

void RemoteVideoRenderer::renderLocalFrame(const QVideoFrame &frame)
{
    m_latestLocalFrame = frame;
    if (!m_localVideoAvailable) {
        m_localVideoAvailable = true;
        emit localVideoAvailableChanged();
    }

    if (m_localVideoSink) {
        m_localVideoSink->setVideoFrame(frame);
    }
}

void RemoteVideoRenderer::renderRemoteFrame(int publisherUid, const QVideoFrame &frame)
{
    // QVideoFrame 使用共享数据，保存最后一帧不会再次复制完整的 YUV 平面。
    m_latestRemoteFrames.insert(publisherUid, frame);

    const auto sink = m_remoteVideoSinks.constFind(publisherUid);
    if (sink != m_remoteVideoSinks.cend() && sink.value()) {
        sink.value()->setVideoFrame(frame);
    }
}

void RemoteVideoRenderer::clearLocalFrame()
{
    m_latestLocalFrame = QVideoFrame();
    if (m_localVideoSink) {
        m_localVideoSink->setVideoFrame(QVideoFrame());
    }

    if (m_localVideoAvailable) {
        m_localVideoAvailable = false;
        emit localVideoAvailableChanged();
    }
}

void RemoteVideoRenderer::clearFrames()
{
    clearLocalFrame();
    m_latestRemoteFrames.clear();

    // 会议结束时给仍然存在的 VideoOutput 提交空帧，让界面立即恢复头像占位，
    // 但保留绑定关系；同一个 MeetingRoomPage 再次入会时无需重新创建本地 VideoOutput。
    for (const QPointer<QVideoSink> &sink : std::as_const(m_remoteVideoSinks)) {
        if (sink) {
            sink->setVideoFrame(QVideoFrame());
        }
    }
}

bool RemoteVideoRenderer::localVideoAvailable() const
{
    return m_localVideoAvailable;
}
