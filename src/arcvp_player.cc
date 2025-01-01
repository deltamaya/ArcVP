//
// Created by maya delta on 2024/12/30.
//

#include <GLFW/glfw3.h>

#include <iostream>

#include "arcvp.h"

extern "C" {
#include <libavutil/imgutils.h>
}
using std::cerr,std::cout;



uint64_t getFramePresentTime(AVFrame *frame, AVRational timeBase) {
  return (frame->pts * timeBase.num) * 1000. / timeBase.den;
}

// player thread
void ArcVP::playerFunc() {
  while (true) {
    std::unique_lock lock{lkPlaybackEvent};
    cvEvent.wait(lock, [this] { return !playbackEvents.empty(); });
    auto request = playbackEvents.front();
    playbackEvents.pop();
    if (request == 0) {
      // prepare the next rgb video frame and present time
      cout<<"Requesting video frame\n";
      auto ok = tryReceiveVideoFrame();
      while (!ok) {
        auto pkt = videoPacketQueue.front();
        avcodec_send_packet(videoCodecContext, pkt);
        av_packet_unref(pkt);
        videoPacketQueue.pop();
        ok = tryReceiveVideoFrame();
      }
      nextVideoFramePresentTimeMs =
          getFramePresentTime(nextVideoFrame, videoStream->time_base);
    } else if (request == 1) {

    }
  }
}

bool ArcVP::getNextFrame() {
  if(curVideoFrame->pts==AV_NOPTS_VALUE) {
    bool ok=tryReceiveVideoFrame();
    while(!ok) {
      auto pkt = videoPacketQueue.front();
      avcodec_send_packet(videoCodecContext, pkt);
      av_packet_unref(pkt);
      videoPacketQueue.pop();
      ok=tryReceiveVideoFrame();
    }
    // curVideoFrame will be a valid frame here

  }

  // sendPresentVideoEvent();
  return ret;
}

// this function will unref the curVideoFrame
bool ArcVP::tryReceiveVideoFrame() {
  int ret = avcodec_receive_frame(videoCodecContext, curVideoFrame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return false;
  }
  return true;
}

// AVFrame *ArcVP::tryReceiveAudioFrame() {
//   int ret = avcodec_receive_frame(audioCodecContext, curAudioFrame);
//   if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//     return nullptr;
//   }
//   return curAudioFrame;
// }
