//
// Created by delta on 7 Oct 2024.
//

#include "player.h"


static int speedIndex = 4;
static float speeds[] = {0.125, 0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2};

float getNextSpeedDown() {
  if (speedIndex > 0) {
    speedIndex--;
  }
  return speeds[speedIndex];
}

float getNextSpeedUp() {
  if (speedIndex + 1 < std::size(speeds)) {
    speedIndex++;
  }
  return speeds[speedIndex];
}
namespace ArcVP {

void Player::setPlaybackSpeed(float speed) {
  spdlog::debug("settings speed to: {}",speed);
  this->speed=speed;
  pause();
  SDL_AudioSpec new_spec;
  new_spec.channels=media_.audio_codec_params_->ch_layout.nb_channels;
  new_spec.format=SDL_AUDIO_F32;
  new_spec.freq=media_.audio_codec_params_->sample_rate*speed;
  SDL_SetAudioStreamFrequencyRatio(audio_stream,speed);
  unpause();
}

void Player::controlPanel() {
  int totalSeconds=ptsToTime(media_.video_stream_->duration,media_.video_stream_->time_base)/1000.;
  int totalMinutes = totalSeconds / 60;
  totalSeconds %= 60;
  int totalHour = totalMinutes / 60;
  totalMinutes %= 60;
  int curSeconds = getPlayedMs() / 1000;
  int curMinutes = curSeconds / 60;
  curSeconds %= 60;
  int curHour = curMinutes / 60;
  curMinutes %= 60;
  double playback_progress=curSeconds*1./totalSeconds;
  // spdlog::debug("total: {}, progress: {}",totalSeconds,playback_progress);
  ImGui::Begin("ArcVP Control Panel");

  if (ImGui::Button("Pause/Unpause")) {

      togglePause();
  }
  if (ImGui::Button("Slow Down")) {

      setPlaybackSpeed(getNextSpeedDown());
  }
  ImGui::SameLine();
  if (ImGui::Button("Speed Up")) {

      setPlaybackSpeed(getNextSpeedUp());
  }
  ImGui::SameLine();
  ImGui::Text("Speed: %.2f", speed);
  ImGui::ProgressBar(playback_progress);
  ImGui::Text("Playback Time: %02d:%02d:%02d / %02d:%02d:%02d", curHour,
              curMinutes, curSeconds, totalHour, totalMinutes, totalSeconds);
  ImGui::End();
}
}  // namespace ArcVP
