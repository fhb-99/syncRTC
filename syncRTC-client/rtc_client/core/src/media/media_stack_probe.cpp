#include <QtGlobal>

#if defined(SYNCRTC_NATIVE_MEDIA_STACK)
#include <QAudioSource>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#if __has_include(<rtc/rtc.hpp>)
#include <rtc/rtc.hpp>
#else
#include <rtc/rtc.h>
#endif

namespace {

// 仅用于让构建系统尽早验证 Qt Multimedia、FFmpeg 和 libdatachannel 的头文件/链接配置。
[[maybe_unused]] void VerifyNativeMediaHeaders()
{
    Q_UNUSED(avcodec_version());
    Q_UNUSED(avformat_version());
    Q_UNUSED(avutil_version());
    Q_UNUSED(swresample_version());
    Q_UNUSED(swscale_version());
}

} // namespace
#endif
