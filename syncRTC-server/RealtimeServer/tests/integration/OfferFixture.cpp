#include <rtc/rtc.hpp>
#include <iostream>

// 生成真实 libdatachannel Offer；只用于信令与资源生命周期联调，不代表音视频通话验收。
int main()
{
    rtc::Configuration config;
    config.disableAutoNegotiation = true;
    config.disableAutoGathering = true;
    auto pc = std::make_shared<rtc::PeerConnection>(config);
    rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(96);
    video.addSSRC(1234, "video", "integration", "video");
    auto video_track = pc->addTrack(video);
    rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(111);
    audio.addSSRC(1235, "audio", "integration", "audio");
    auto audio_track = pc->addTrack(audio);
    pc->setLocalDescription();
    std::cout << std::string(*pc->localDescription());
    pc->close();
}
