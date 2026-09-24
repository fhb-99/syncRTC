#include "mediasession.h"
#include "../models/global.h"

#include <qdebug.h>

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

void MediaSession::startMediaSession(const QString &meetingId)
{
    m_meetingId = meetingId;

    rtc::Configuration config;
    config.disableAutoNegotiation = true;

    // STUN 用于让客户端在 NAT 后收集 srflx candidate。candidate 仍通过现有
    // localCandidateReady -> TcpMgr -> RealtimeServer 控制链路发送给 MediaServer。
    const quint16 stunPort = WebRtcStunPort.toUShort();
    if (!WebRtcStunHost.isEmpty() && stunPort != 0) {
        config.iceServers.emplace_back(WebRtcStunHost.toStdString(), stunPort);
    }

    // 当前云端 coturn 已开放 UDP 3478 与 UDP relay 端口范围。仅在运行配置提供完整
    // TURN 凭据时才加入 TurnUdp，避免把空密码配置成无效 relay server。
    const quint16 turnPort = WebRtcTurnPort.toUShort();
    if (!WebRtcTurnHost.isEmpty() && turnPort != 0
        && !WebRtcTurnUsername.isEmpty() && !WebRtcTurnPassword.isEmpty()) {
        config.iceServers.emplace_back(
            WebRtcTurnHost.toStdString(),
            turnPort,
            WebRtcTurnUsername.toStdString(),
            WebRtcTurnPassword.toStdString(),
            rtc::IceServer::RelayType::TurnUdp);
    }

    m_peerConnection = std::make_shared<rtc::PeerConnection>(config);

    m_peerConnection->onLocalDescription([this](rtc::Description description) {
        const QString sdp = QString::fromStdString(std::string(description));
        if (description.type() == rtc::Description::Type::Answer) {
            // MediaServer 主动 Offer 的本地响应必须走独立的 Answer 请求，
            // 不能误用初次建连时的客户端 Offer 协议号。
            emit localAnswerReady(m_meetingId, sdp);
            return;
        }

        qDebug() << "========== LOCAL DESCRIPTION ==========";
        qDebug() << "type:"
                 << QString::fromStdString(
                        rtc::Description::typeToString(
                            description.type()));

        qDebug() << "pc address:"
                 << static_cast<void*>(m_peerConnection.get());

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
        qDebug() << "远端音视频数据包已接受";
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
    // Track 只由 RtcTransportWorker 访问；释放后，该线程中的 RTP 队列不会再写入旧连接。
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

void MediaSession::sendVideoRtp(const QByteArray &packet)
{
    if (packet.isEmpty() || !m_videoTrack || !m_videoTrack->isOpen()) {
        return;
    }

    m_videoTrack->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                       static_cast<size_t>(packet.size()));
}

void MediaSession::sendAudioRtp(const QByteArray &packet)
{
    if (packet.isEmpty() || !m_audioTrack || !m_audioTrack->isOpen()) {
        return;
    }

    m_audioTrack->send(reinterpret_cast<const rtc::byte *>(packet.constData()),
                       static_cast<size_t>(packet.size()));
}
