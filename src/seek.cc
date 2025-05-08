//
// Created by delta on 1/22/2025.
//

#include "player.h"


using namespace std::chrono;

namespace ArcVP {
void Player::seekTo(std::int64_t milli){
  std::scoped_lock lk{sync_state_.mtx_};

  spdlog::debug("seek to {}s",milli/1000.);

  video_packet_channel_.clear();
  audio_packet_channel_.clear();
  
  spdlog::debug("render queue before seek size: {}",video_frame_channel_.size());

  while (!video_frame_channel_.empty()&&video_frame_channel_.front()->present_ms<milli) {
    video_frame_channel_.receive();
  }

  spdlog::debug("render queue after seek size: {}",video_frame_channel_.size());

  spdlog::debug("audio queue before seek size: {}",audio_frame_channel_.size());

  while (!audio_frame_channel_.empty()&&audio_frame_channel_.front()->present_ms<milli) {
    audio_frame_channel_.receive();
  }
  spdlog::debug("audio queue after seek size: {}",audio_frame_channel_.size());



  if(media_context_.video_codec_context_!=nullptr) {
    int64_t ts=timeToPts(milli,media_context_.video_stream_->time_base);
    spdlog::debug("video pts: {}",ts);
    int ret=av_seek_frame(media_context_.format_context_,media_context_.video_stream_index_,ts,AVSEEK_FLAG_BACKWARD);
    if(ret<0) {
      spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
      return;
    }
    avcodec_flush_buffers(media_context_.video_codec_context_);
  }

  if(media_context_.audio_codec_context_!=nullptr) {
    int64_t ts=timeToPts(milli,media_context_.audio_stream_->time_base);
    spdlog::debug("audio pts: {}",ts);

    int ret=av_seek_frame(media_context_.format_context_,media_context_.audio_stream_index_,ts,AVSEEK_FLAG_BACKWARD);
    if(ret<0) {
      spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
      return;
    }
    avcodec_flush_buffers(media_context_.audio_codec_context_);
  }
}

// void Player::speedUp(){
//   std::unique_lock lk{videoMtx};
//   if(speed<1) {
//     speed=1;
//   }else if(speed<1.5){
//     speed=1.5;
//   }else {
//     speed=2;
//   }
//   auto pTimeMilli=ptsToTime(prevFramePts,videoStream->time_base);
//   bool p=pause.load();
//   if (audioDeviceID > 0) {
//     SDL_CloseAudioDevice(audioDeviceID);
//   }
//   if(!p) {
//     togglePause();
//     setupAudioDevice(audioCodecParams->sample_rate*speed);
//     togglePause();
//   }else {
//     setupAudioDevice(audioCodecParams->sample_rate*speed);
//   }
//
//
//
//   videoStart = system_clock::now() - milliseconds(static_cast<int>( pTimeMilli / speed ));
// }
//
// void Player::speedDown(){
//   std::unique_lock lk{videoMtx};
//   if(speed>1.5) {
//     speed=1.5;
//   }else if(speed>1) {
//     speed=1;
//   }else {
//     speed=0.5;
//   }
//   bool p=pause.load();
//   if (audioDeviceID > 0) {
//     SDL_CloseAudioDevice(audioDeviceID);
//   }
//   if(!p) {
//     togglePause();
//     setupAudioDevice(audioCodecParams->sample_rate*speed);
//     togglePause();
//   }else {
//     setupAudioDevice(audioCodecParams->sample_rate*speed);
//   }
//
//   auto pTimeMilli=ptsToTime(prevFramePts,videoStream->time_base);
//   videoStart = system_clock::now() - milliseconds(static_cast<int>( pTimeMilli / speed ));
// }
}