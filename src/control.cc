//
// Created by delta on 5/9/2025.
//
#include "player.h"
namespace ArcVP {
void Player::startPlayback() {
  if (audio_device_.id==-1) {
    setupAudioDevice();
  }
  spdlog::info("default audio device: {}", audio_device_.name);
  audio_stream=SDL_CreateAudioStream(&audio_device_.spec,&audio_device_.spec);
  if (!audio_stream) {
    spdlog::error("fail to get audio stream: {}",SDL_GetError());
    std::exit(1);
  }
  SDL_BindAudioStream(audio_device_.id,audio_stream);

  packet_decode_worker_.spawn([this] { this->packetDecodeThreadWorker(); });
  audio_decode_worker_.spawn([this] { this->audioDecodeThreadWorker(); });
  video_decode_worker_.spawn([this] { this->videoDecodeThreadWorker(); });

  sync_state_.status_ = InstanceStatus::Playing;

  SDL_ResumeAudioDevice(audio_device_.id);
}

void Player::pause() {
  sync_state_.status_ = InstanceStatus::Pause;
  SDL_PauseAudioDevice(audio_device_.id);
}

void Player::unpause() {
  sync_state_.status_ = InstanceStatus::Playing;
  SDL_ResumeAudioDevice(audio_device_.id);
}

}  // namespace ArcVP