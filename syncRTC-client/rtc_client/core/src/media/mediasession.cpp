#include "mediasession.h"

#if __has_include(<rtc/rtc.hpp>)
#include <rtc/rtc.hpp>
#else
#include <rtc/rtc.h>
#endif

MediaSession::MediaSession(QObject *parent)
    : QObject(parent)
{
}

MediaSession::~MediaSession() = default;

std::shared_ptr<rtc::Track> MediaSession::videoTrack() const
{
    return m_videoTrack;
}

std::shared_ptr<rtc::Track> MediaSession::audioTrack() const
{
    return m_audioTrack;
}

void MediaSession::startMediaSession(const QString &meetingId)
{
    m_meetingId = meetingId;

    rtc::Configuration config;
    config.disableAutoNegotiation = true;

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onLocalDescription([this](rtc::Description description) {
        const QString sdp = QString::fromStdString(std::string(description));
        if (description.type() == rtc::Description::Type::Answer) {
            // MediaServer 主动 Offer 的本地响应必须走独立的 Answer 请求，
            // 不能误用初次建连时的客户端 Offer 协议号。
            emit localAnswerReady(m_meetingId, sdp);
            return;
        }
        emit localOfferReady(m_meetingId, sdp);
    });

    m_peerConnection->onLocalCandidate([this](rtc::Candidate candidate) {
        emit localCandidateReady(m_meetingId,
                                 QString::fromStdString(std::string(candidate)),
                                 QString::fromStdString(candidate.mid()));
    });

    m_peerConnection->onTrack([this](std::shared_ptr<rtc::Track> track) {
        const QString mid = QString::fromStdString(track->mid());
        const int publisherUid = mid.mid(mid.lastIndexOf('-') + 1).toInt();

        if (track->description().type() == "video") {
            // libdatachannel 已在 PeerConnection 内部完成 DTLS/SRTP 解密，Track 收到的是 RTP 包。
            // H264RtpDepacketizer 会去掉 RTP 头，并把同一个 RTP 时间戳下的单 NALU、STAP-A
            // 或 FU-A 分片重新组合成一帧 H.264 Annex-B 编码数据，再触发 onFrame。
            track->setMediaHandler(std::make_shared<rtc::H264RtpDepacketizer>());
            track->onFrame([this, publisherUid](rtc::binary frame, rtc::FrameInfo frameInfo) {
                // 回调运行在 libdatachannel 的收包线程。QByteArray 在这里复制编码帧，
                // 后续接收模块可以通过 Qt 队列连接把它安全地交给解码线程。
                emit remoteVideoEncodedFrameReady(
                    publisherUid,
                    QByteArray(reinterpret_cast<const char *>(frame.data()),
                               static_cast<qsizetype>(frame.size())),
                    frameInfo.timestamp);
            });
        } else {
            // OpusRtpDepacketizer 去掉 RTP 头后，每次 onFrame 交出一个完整 Opus 编码帧。
            // 此处不做解码、播放或音画同步，只把编码帧继续交给客户端接收链路。
            track->setMediaHandler(std::make_shared<rtc::OpusRtpDepacketizer>());
            track->onFrame([this, publisherUid](rtc::binary frame, rtc::FrameInfo frameInfo) {
                emit remoteAudioEncodedFrameReady(
                    publisherUid,
                    QByteArray(reinterpret_cast<const char *>(frame.data()),
                               static_cast<qsizetype>(frame.size())),
                    frameInfo.timestamp);
            });
        }

        // 服务端 Offer 中每个新增 m-line 都会创建一个接收 Track。配置完回调后仍需持有 Track，
        // 否则对象释放会让该媒体线路停止接收，生成 Answer 时也可能将对应 m-line 标记为拒绝。
        std::lock_guard<std::mutex> lock(m_remoteTracksMutex);
        m_remoteTracks.push_back(std::move(track));
    });

    rtc::Description::Video video("video");
    video.addH264Codec(96);
    video.addSSRC(123456, "video", "syncRTC", "video");

    rtc::Description::Audio audio("audio");
    audio.addOpusCodec(111);
    audio.addSSRC(654321, "audio", "syncRTC", "audio");

    // 这两个Track属于客户端与MediaServer之间的PeerConnection。
    // addTrack把固定的编解码器、Payload Type和SSRC写入本地offer；MediaServer返回answer并完成ICE后，
    // Track会进入Open状态。之后写入Track的RTP包会由libdatachannel完成DTLS/SRTP保护并发送给MediaServer。
    // offer/candidate仍由上层通过TcpMgr发给RealtimeServer，它们只是协商信令，不承载音视频数据。
    m_videoTrack = m_peerConnection->addTrack(video);
    m_audioTrack = m_peerConnection->addTrack(audio);
    m_peerConnection->setLocalDescription();
}

void MediaSession::stopMediaSession()
{
    // 释放Track后MediaTransportMgr中的弱引用会自动失效，后续采集包不会再进入旧连接。
    m_videoTrack.reset();
    m_audioTrack.reset();
    m_peerConnection.reset();
    {
        std::lock_guard<std::mutex> lock(m_remoteTracksMutex);
        m_remoteTracks.clear();
    }
    m_meetingId.clear();
}

void MediaSession::setRemoteDescription(const QString &sdp, const QString &type)
{
    // SDP 是整场 WebRTC 协商的说明书：包含编解码器、媒体方向、DTLS 指纹等信息。
    m_peerConnection->setRemoteDescription(
        rtc::Description(sdp.toStdString(), type.toStdString()));
}

void MediaSession::setRemoteOffer(const QString &sdp)
{
    // 先应用 MediaServer 的 Offer，使 libdatachannel 创建新增的接收 Track；
    // 再显式生成 Answer。客户端关闭了自动协商，所以这一步不能省略。
    m_peerConnection->setRemoteDescription(rtc::Description(sdp.toStdString(), "offer"));
    m_peerConnection->setLocalDescription();
}

void MediaSession::addRemoteCandidate(const QString &candidate, const QString &mid)
{
    // ICE candidate 是对端的可连接地址；设置后 PeerConnection 会尝试打通网络路径。
    m_peerConnection->addRemoteCandidate(
        rtc::Candidate(candidate.toStdString(), mid.toStdString()));
}
