//
// Created by delta on 7 Oct 2024.
//

#include "video-reader.hh"
#include "imgui.h"
#include "portable-file-dialogs.h"

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

void VideoReader::resetSpeed() {
  playbackSpeed = 1.0;
  speedIndex = 4;
}


void VideoReader::controlPanel() {
  int totalSeconds = durationMilli / 1000;
  int totalMinutes = totalSeconds / 60;
  totalSeconds %= 60;
  int totalHour = totalMinutes / 60;
  totalMinutes %= 60;
  int curSeconds = videoPlayTimeMilli / 1000;
  int curMinutes = curSeconds / 60;
  curSeconds %= 60;
  int curHour = curMinutes / 60;
  curMinutes %= 60;
  ImGui::Begin("ArcVP Control Panel");
  ImVec2 pos = ImGui::GetWindowPos();
  controlPanelPosX = pos.x;
  controlPanelPosY = pos.y;
  ImVec2 sz = ImGui::GetWindowSize();
  controlPanelSizeWidth = sz.x;
  controlPanelSizeHeight = sz.y;
  spdlog::debug("control panel pos: {} {} {} {}",
                controlPanelPosX,
                controlPanelPosY,
                controlPanelSizeWidth,
                controlPanelSizeHeight);
  if (ImGui::Button("Choose File")) {
    close();
    if (chooseFile()) {
      startDecode();
      startPlayback();
      ImGui::End();
      return;
    }
    if (!defaultScreenThread_) {
      startDefaultScreen();
    } else {
      if (curRenderer) {
        SDL_DestroyRenderer(curRenderer);
        curRenderer = nullptr;
      }
      curRenderer = SDL_CreateRenderer(curWindow, -1, SDL_RENDERER_ACCELERATED);
      if (!curRenderer) {
        spdlog::error("Unable to create video renderer: {}", SDL_GetError());
        std::exit(1);
      }
      int width, height;
      SDL_GetWindowSize(curWindow, &width, &height);
      SDL_RenderSetLogicalSize(curRenderer, width, height);
      ImGuiSDL::Initialize(curRenderer, width, height);
      spdlog::info("default screen thread started");
    }
    ImGui::End();
    return;
  }

  if (ImGui::Button("Pause/Unpause")) {
    if (!playbackThread_) {
      pfd::message msg("Notice", "Please choose a video to play.", pfd::choice::ok);
    } else {
      togglePause();
    }
  }
  if (ImGui::Button("Slow Down")) {
    if (!playbackThread_) {
      pfd::message msg("Notice", "Please choose a video to play.", pfd::choice::ok);
    } else {
      setPlaybackSpeed(getNextSpeedDown());
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Speed Up")) {
    if (!playbackThread_) {
      pfd::message msg("Notice", "Please choose a video to play.", pfd::choice::ok);
    } else {
      setPlaybackSpeed(getNextSpeedUp());
    }
  }
  ImGui::SameLine();
  ImGui::Text("Speed: %.2f", playbackSpeed);
  static std::int64_t lastFrameUpdateTime = SDL_GetTicks64();
  if (playbackThread_) {
    if (ImGui::SliderFloat("Video Progress", &playbackProgress, 0., 1., "", ImGuiSliderFlags_AlwaysClamp)) {
      int estimatePlaytimeSeconds = durationMilli / 1000. * playbackProgress;
      int estimatePlaytimeMinutes = estimatePlaytimeSeconds / 60;
      estimatePlaytimeSeconds %= 60;
      int estimatePlaytimeHour = estimatePlaytimeMinutes / 60;
      estimatePlaytimeMinutes %= 60;
      curSeconds = estimatePlaytimeSeconds;
      curMinutes = estimatePlaytimeMinutes;
      curHour = estimatePlaytimeHour;
    }
    if (ImGui::IsItemActive()) {
      if (SDL_GetTicks64() - lastFrameUpdateTime > 300) {
        lastFrameUpdateTime = SDL_GetTicks64();
        std::int64_t seekTo = playbackProgress * durationMilli;
        seekFrame(seekTo);
        setPause();
        showNextFrame();
      }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      // This will be triggered only after the slider is released
      std::int64_t seekTo = playbackProgress * durationMilli;
      seekFrame(seekTo);
      setUnpause();
    }
  }
  ImGui::ProgressBar(playbackProgress);
  ImGui::Text("Playback Time: %02d:%02d:%02d / %02d:%02d:%02d",
              curHour,
              curMinutes,
              curSeconds,
              totalHour,
              totalMinutes,
              totalSeconds);
  ImGui::End();
}
