//
// Created by delta on 1 Oct 2024.
//

#include "video-reader.hh"


void VideoReader::setPlaybackSpeed(double speed) {
  if (speed < 0.125 || speed > 2) {
    spdlog::warn("setPlaybackSpeed: invalid speed value");
    return;
  }
  if (playbackSpeed == speed) {
    spdlog::info("setPlaybackSpeed: playback speed is same");
    return;
  }
  if (audioDeviceId > 0) {
    SDL_CloseAudioDevice(audioDeviceId);
  }
  setupAudioDevice(audioCodecContext_->sample_rate * speed);
  SDL_PauseAudioDevice(audioDeviceId, false);
  playbackSpeed = speed;
  auto curTime = SDL_GetTicks64();
  videoEntryPoint = curTime - videoPlayTimeMilli * 1. / speed;
  audioEntryPoint = curTime - videoPlayTimeMilli * 1. / speed;
  spdlog::info("setPlaybackSpeed: playback speed set to {}", speed);
}

void VideoReader::setPause() {
  pauseVideo.test_and_set();
  SDL_PauseAudioDevice(audioDeviceId, true);
}

void VideoReader::setUnpause() {
  pauseVideo.clear();
  SDL_PauseAudioDevice(audioDeviceId, false);
  auto curTime = SDL_GetTicks64();
  videoEntryPoint = curTime - videoPlayTimeMilli * 1. / playbackSpeed;
  audioEntryPoint = curTime - videoPlayTimeMilli * 1. / playbackSpeed;
  spdlog::debug("Set entry point: {}", videoEntryPoint.load());
}

void VideoReader::togglePause() {
  if (pauseVideo.test()) {
    setUnpause();
  } else {
    setPause();
  }
}

bool VideoReader::setSize(const int width, const int height) {

  std::unique_lock lkWindow{mutexWindow_};
  // Destroy the renderer and texture
  if(curTexture_)
    SDL_DestroyTexture(curTexture_);
  if(curRenderer)
    SDL_DestroyRenderer(curRenderer);
  // Recreate renderer and texture
  curRenderer = SDL_CreateRenderer(curWindow, -1, SDL_RENDERER_ACCELERATED);
  curTexture_ = SDL_CreateTexture(curRenderer,
                                  SDL_PIXELFORMAT_YV12,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  frameWidth,
                                  frameHeight);
  if (!curRenderer || !curTexture_) {
    printf("Error resetting renderer/texture: %s\n", SDL_GetError());
  }
  SDL_RenderSetLogicalSize(curRenderer, width, height);
  double aspectRatio = frameHeight * 1. / frameWidth;
  double curRatio = height * 1. / width;
  if (curRatio > aspectRatio) {
    int displayHeight = std::min(static_cast<int>(width * aspectRatio), height);
    int heightDelta = (height - displayHeight) / 2;
    destRect_ = {0, heightDelta, width, displayHeight};
  } else if (curRatio < aspectRatio) {
    int displayWidth = std::min(static_cast<int>(height / aspectRatio), width);
    int widthDelta = (width - displayWidth) / 2;
    destRect_ = {widthDelta, 0, displayWidth, height};
  } else {
    destRect_ = {0, 0, width, height};
  }
  spdlog::debug("set size done");
  return true;
}