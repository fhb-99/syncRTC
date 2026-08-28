#include "../src/media/render/remotevideorenderer.h"

#include <QSignalSpy>
#include <QSize>
#include <QTest>
#include <QVideoFrameFormat>
#include <QVideoSink>

class RemoteVideoRendererTest : public QObject
{
    Q_OBJECT

private slots:
    void cachedRemoteFrameIsRenderedAfterQmlSinkBinds()
    {
        RemoteVideoRenderer renderer;
        QVideoFrame frame(QVideoFrameFormat(
            QSize(64, 64), QVideoFrameFormat::Format_YUV420P));
        QVideoSink sink;

        // 媒体帧可能先于成员区域到达。绑定 QML VideoOutput 的 sink 后，
        // 应立即补交缓存帧，而不是继续显示头像等待下一帧。
        renderer.renderRemoteFrame(42, frame);
        renderer.bindRemoteVideoSink(42, &sink);

        QVERIFY(sink.videoFrame() == frame);
    }

    void unboundRemoteSinkNoLongerReceivesFrames()
    {
        RemoteVideoRenderer renderer;
        QVideoSink sink;
        QVideoFrame firstFrame(QVideoFrameFormat(
            QSize(64, 64), QVideoFrameFormat::Format_YUV420P));
        QVideoFrame secondFrame(QVideoFrameFormat(
            QSize(96, 54), QVideoFrameFormat::Format_YUV420P));

        renderer.bindRemoteVideoSink(7, &sink);
        renderer.renderRemoteFrame(7, firstFrame);
        renderer.unbindRemoteVideoSink(7, &sink);
        renderer.renderRemoteFrame(7, secondFrame);

        QVERIFY(sink.videoFrame() == firstFrame);
    }

    void clearingLocalFrameRestoresAvatarState()
    {
        RemoteVideoRenderer renderer;
        QVideoSink sink;
        QSignalSpy availabilitySpy(&renderer,
                                   &RemoteVideoRenderer::localVideoAvailableChanged);
        QVideoFrame frame(QVideoFrameFormat(
            QSize(64, 64), QVideoFrameFormat::Format_YUV420P));

        renderer.bindLocalVideoSink(&sink);
        renderer.renderLocalFrame(frame);
        QVERIFY(renderer.localVideoAvailable());
        QVERIFY(sink.videoFrame().isValid());

        renderer.clearLocalFrame();
        QVERIFY(!renderer.localVideoAvailable());
        QVERIFY(!sink.videoFrame().isValid());
        QCOMPARE(availabilitySpy.count(), 2);
    }
};

QTEST_GUILESS_MAIN(RemoteVideoRendererTest)

#include "remotevideorenderer_test.moc"
