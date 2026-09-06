# Qt 客户端原生音视频依赖配置

本文记录 SyncRTC Qt 客户端后续接入原生音视频链路时的构建依赖。

## 推荐链路

```text
Qt Multimedia
  采集摄像头 QVideoFrame / 麦克风 PCM

FFmpeg
  像素格式转换 / 音频重采样 / H.264 或 Opus 编码

RTP packetizer
  生成 WebRTC Track 需要的 RTP 包、时间戳、序列号、SSRC

libdatachannel
  PeerConnection / ICE / DTLS / SRTP / Track 发送
```

注意：这里的 FFmpeg 不负责把数据封装成 MP4、FLV、MKV 这类文件容器。实时 WebRTC 链路要送入
`libdatachannel` Track 的是 RTP 包。

## CMake 开关

默认构建启用 `Qt6::Multimedia` 和 FFmpeg，但不要求机器已经安装 libdatachannel：

```powershell
cmake -S . -B build/default `
  -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH=D:/Qt6/6.5.3/mingw_64 `
  -DCMAKE_CXX_COMPILER=D:/Qt6/Tools/mingw1120_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/Qt6/Tools/mingw1120_64/bin/mingw32-make.exe `
  -DFFMPEG_ROOT=E:/ffmpge-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1
```

安装 libdatachannel 后，打开 WebRTC 原生媒体栈检查：

```powershell
cmake -S . -B build/native-media `
  -G "MinGW Makefiles" `
  -DCMAKE_PREFIX_PATH="D:/Qt6/6.5.3/mingw_64;<libdatachannel-install-prefix>" `
  -DCMAKE_CXX_COMPILER=D:/Qt6/Tools/mingw1120_64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/Qt6/Tools/mingw1120_64/bin/mingw32-make.exe `
  -DSYNCRTC_ENABLE_NATIVE_MEDIA_STACK=ON `
  -DFFMPEG_ROOT=E:/ffmpge-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1
```

## 当前本机状态

已识别到 FFmpeg 开发包：

```text
E:/ffmpge-5.1.6/ffmpeg-n5.1.6-9-gdcdfd7fb62-win64-gpl-shared-5.1
```

尚未识别到 libdatachannel。安装后把它的 CMake 安装前缀加入 `CMAKE_PREFIX_PATH` 即可。
