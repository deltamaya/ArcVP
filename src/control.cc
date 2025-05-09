//
// Created by delta on 5/9/2025.
//
#include "player.h"
namespace ArcVP {
void Player::startPlayback(){
  SDL_GetDefaultAudioInfo(&audio_device_.name, &audio_device_.spec, false);
  spdlog::info("default audio device: {}", audio_device_.name);
  if (!setupAudioDevice(media_context_.audio_codec_context_->sample_rate)) {
    spdlog::info("Unable to setup audio device: {}", audio_device_.name);
    return;
  }

  packet_decode_worker_.spawn([this]{ this->packetDecodeThreadWorker(); });
  audio_decode_worker_.spawn([this]{ this->audioDecodeThreadWorker(); });
  video_decode_worker_.spawn([this]{ this->videoDecodeThreadWorker(); });

  sync_state_.status_=InstanceStatus::Playing;

  SDL_PauseAudioDevice(audio_device_.id, false);
}

void Player::pause() {
  std::scoped_lock lk{sync_state_.mtx_};
  sync_state_.status_=InstanceStatus::Pause;
  SDL_PauseAudioDevice(audio_device_.id,true);
}

}