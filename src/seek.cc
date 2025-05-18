//
// Created by delta on 1/22/2025.
//

#include "player.h"


using namespace std::chrono;

namespace ArcVP {
void Player::seekTo(std::int64_t milli){
  std::scoped_lock lk{video_decode_worker_.mtx,audio_decode_worker_.mtx};
  pause();

  spdlog::debug("current: {}s,seek to {}s",getPlayedMs()/1000.,milli/1000.);


  if(media_.video_codec_context_!=nullptr) {
    int64_t ts=timeToPts(milli,media_.video_stream_->time_base);
    spdlog::debug("video pts: {}",ts);
    int ret=av_seek_frame(media_.format_context_,media_.video_stream_index_,ts,AVSEEK_FLAG_BACKWARD);
    if(ret<0) {
      spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
      return;
    }
    avcodec_flush_buffers(media_.video_codec_context_);
  }

  if(media_.audio_codec_context_!=nullptr) {
    int64_t ts=timeToPts(milli,media_.audio_stream_->time_base);
    spdlog::debug("audio pts: {}",ts);

    int ret=av_seek_frame(media_.format_context_,media_.audio_stream_index_,ts,AVSEEK_FLAG_BACKWARD);
    if(ret<0) {
      spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
      return;
    }
    avcodec_flush_buffers(media_.audio_codec_context_);
  }
  sync_state_.sample_count_=(milli/1000.)*media_.audio_codec_params_->sample_rate;

  while (!video_decode_worker_.output_queue.queue.empty()) {
    video_decode_worker_.output_queue.semReady.acquire();
    video_decode_worker_.output_queue.mtx.lock();
    auto front=video_decode_worker_.output_queue.queue.front();
    av_frame_free(&front.frame);
    video_decode_worker_.output_queue.queue.pop_front();
    video_decode_worker_.output_queue.mtx.unlock();
    video_decode_worker_.output_queue.semEmpty.release();
  }

  while (!audio_decode_worker_.output_queue.queue.empty()) {
    audio_decode_worker_.output_queue.semReady.acquire();
    audio_decode_worker_.output_queue.mtx.lock();
    auto front=audio_decode_worker_.output_queue.queue.front();
    av_frame_free(&front.frame);
    audio_decode_worker_.output_queue.queue.pop_front();
    audio_decode_worker_.output_queue.mtx.unlock();
    audio_decode_worker_.output_queue.semEmpty.release();
  }

  while (!video_decode_worker_.packet_chan.empty()) {
    av_packet_free(&video_decode_worker_.packet_chan.front());
    video_decode_worker_.packet_chan.pop_front();
  }

  while (!audio_decode_worker_.packet_chan.empty()) {
    av_packet_free(&audio_decode_worker_.packet_chan.front());
    audio_decode_worker_.packet_chan.pop_front();
  }
  demuxAllPackets();
  bool ok= SDL_ClearAudioStream(audio_stream);
  if (!ok) {
    spdlog::error("Unable to clear audio stream: {}",SDL_GetError());
  }

  while (true) {
    AVFrame* frame=decodeVideoFrame();
    if (!frame) {
      video_decode_worker_.output_queue.queue.emplace_back(nullptr,AV_NOPTS_VALUE);
      break;
    }
    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms = ptsToTime(frame->pts, media_.video_stream_->time_base);
    if (present_ms<milli) {
      av_frame_free(&frame);
      continue;
    }

    video_decode_worker_.output_queue.mtx.lock();
    if (!video_decode_worker_.output_queue.queue.empty()) {
      video_decode_worker_.output_queue.semReady.acquire();
      auto front=video_decode_worker_.output_queue.queue.front();
      av_frame_free(&front.frame);
      video_decode_worker_.output_queue.queue.pop_front();
      video_decode_worker_.output_queue.semEmpty.release();
    }

    video_decode_worker_.output_queue.semEmpty.acquire();
    video_decode_worker_.output_queue.queue.emplace_back(frame,present_ms);
    spdlog::debug("put video frame at: {}",present_ms);
    video_decode_worker_.output_queue.mtx.unlock();
    video_decode_worker_.output_queue.semReady.release();
    break;
  }

  while (true) {
    AVFrame* frame=decodeAudioFrame();
    if (!frame) {
      break;
    }
    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms = ptsToTime(frame->pts, media_.audio_stream_->time_base);
    if (present_ms<milli) {
      av_frame_free(&frame);
      continue;
    }
    spdlog::debug("present audio frame ms: {}",present_ms);

    resampleAudioFrame(frame);
    SDL_PutAudioStreamData(audio_stream,audio_buffer_.data(),audio_buffer_.size());
    spdlog::debug("SDL audio stream avail data size: {}",SDL_GetAudioStreamAvailable(audio_stream));
    sync_state_.sample_count_ +=
      audio_buffer_.size() / sizeof(float) /
      media_.audio_codec_params_->ch_layout.nb_channels;
    SDL_FlushAudioStream(audio_stream);
    av_frame_free(&frame);
    break;
  }

  unpause();
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