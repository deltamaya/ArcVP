//
// Created by delta on 1/22/2025.
//


#include "player.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace  ArcVP {
void pushFinishEvent(){
  SDL_Event event;
  event.type = ARCVP_FINISH_EVENT;
  SDL_PushEvent(&event);
}


void Player::startPlayback(){
  SDL_GetDefaultAudioInfo(&audio_device_.name, &audio_device_.spec, false);
  spdlog::info("default audio device: {}", audio_device_.name);
  if (!setupAudioDevice(media_context_.audio_codec_context_->sample_rate)) {
    spdlog::info("Unable to setup audio device: {}", audio_device_.name);
    return;
  }

  if (!packet_decode_thread_) {
    packet_decode_thread_ = std::make_unique<std::thread>([this]{ this->packetDecodeThreadWorker(); });
  }
  if (!video_decode_thread_) {
    video_decode_thread_ = std::make_unique<std::thread>([this]{ this->videoDecodeThreadWorker(); });
  }
  if (!audio_decode_thread_) {
    audio_decode_thread_ = std::make_unique<std::thread>([this]{ this->audioDecodeThreadWorker(); });
  }
  packet_decode_worker_status_ = WorkerStatus::Working;
  video_decode_worker_status_ = WorkerStatus::Working;
  audio_decode_worker_status_=WorkerStatus::Working;
  SDL_PauseAudioDevice(audio_device_.id, false);
}

void Player::packetDecodeThreadWorker(){
  // demux all packets from the format context
  while (true) {
    while (packet_decode_worker_status_ == WorkerStatus::Idle) {
      std::this_thread::sleep_for(10ms);
    }
    if (packet_decode_worker_status_ == WorkerStatus::Exiting) {
      break;
    }
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
      spdlog::error("Fail to allocate AVPacket");
      std::exit(1);
    }
    int ret; {
      std::scoped_lock lk{media_context_.format_mtx_};
      ret = av_read_frame(media_context_.format_context_, pkt);
    }
    if (ret < 0) {
      av_packet_free(&pkt);
      if (ret == AVERROR_EOF) {
        packet_decode_worker_status_ = WorkerStatus::Idle;
        continue;
      }
      spdlog::error("Error reading frame: {}", av_err2str(ret));
      std::exit(1);
    }
    if (pkt->stream_index == media_context_.video_stream_index_) {
      video_packet_channel_.send(pkt);
    }
    else if (pkt->stream_index == media_context_.audio_stream_index_) {
      video_packet_channel_.send(pkt);
    }
    else {
      spdlog::warn("Unknown packet index: {}", pkt->stream_index);
      av_packet_free(&pkt);
    }
  }
  spdlog::info("Decoder Thread Exited");
}
}