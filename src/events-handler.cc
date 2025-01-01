//
// Created by delta on 1 Oct 2024.
//
#include "video-reader.hh"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

static bool showControlPanel = true;
static int64_t lastMoveTimeMilli = 0;
constexpr int64_t idleTimeToHideControlPanel = 5000;//ms

bool VideoReader::handleEvent(const SDL_Event &event) {
  std::int64_t curTime = SDL_GetTicks64();
  if (curTime - lastMoveTimeMilli > idleTimeToHideControlPanel) {
    idle();
  }
  switch (event.type) {
    case SDL_WINDOWEVENT: {
      switch (event.window.event) {
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
          int newWidth = event.window.data1;
          int newHeight = event.window.data2;
          setSize(newWidth, newHeight);
          break;
        }
        case SDL_WINDOWEVENT_CLOSE:spdlog::debug("SDL_WINDOWEVENT_CLOSE");
          return true;
        default:break;
      }
      break;
    }
    case SDL_QUIT: {
      spdlog::debug("SDL_QUIT");
      return true;
    }
    case SDL_KEYDOWN: {
      handleKeyDown(event, curTime);
      active(curTime);
      break;
    }
    case SDL_MOUSEBUTTONDOWN: {
      if (playbackThread_) {
        int posX, posY;
        SDL_GetMouseState(&posX, &posY);
        spdlog::debug("mouse pos: {} {}", posX, posY);
        // click in the control panel
        if (posX > controlPanelPosX && posX < controlPanelPosX + controlPanelSizeWidth
            && posY > controlPanelPosY && posY < controlPanelPosY + controlPanelSizeHeight) {
          break;
        } else {
          togglePause();
        }
      }
      break;
    }
    case SDL_PLAYER_RENDER_EVENT: {
      if (playing_.test()) {
        spdlog::debug("renderer event");
        ImGui::NewFrame();
        if (showControlPanel) {
          controlPanel();
        }
        std::unique_lock lkWindow{mutexWindow_};
        ImGui::Render();
        SDL_RenderPresent(curRenderer);
      }
      break;
    }
    case SDL_MOUSEMOTION: {
      active(curTime);
      break;
    }
    case SDL_PLAYER_DEFAULT_SCREEN_EVENT: {
      if (!playbackThread_) {
        std::unique_lock lkWindow{mutexWindow_};
        SDL_RenderClear(curRenderer);
        // Start new frame for ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        controlPanel();
        ImGui::Render();
        SDL_RenderPresent(curRenderer);
      }
      break;
    }
    case SDL_PLAYER_DECODE_FINISH_EVENT: {
      decoding_.clear();
      decodeThread_->join();
      decodeThread_ = nullptr;
      break;
    }
    case SDL_PLAYER_PLAYBACK_FINISH_EVENT: {
      playing_.clear();
      playbackThread_->join();
      playbackThread_ = nullptr;
      startDefaultScreen();
      break;
    }
    default: {

    }
  }
  return false;
}

void VideoReader::active(std::int64_t curTime) {
  lastMoveTimeMilli = curTime;
  showControlPanel = true;
  SDL_ShowCursor(SDL_ENABLE);
}

void VideoReader::idle() {
  showControlPanel = false;
  SDL_ShowCursor(SDL_DISABLE);
}

static bool fullScreen = false;

void VideoReader::handleKeyDown(SDL_Event event, std::int64_t curTime) {
  switch (event.key.keysym.sym) {
    case SDLK_RIGHT: {
      spdlog::debug("Right Arrow pressed SDL {}ms, cur playtime: {}ms", curTime, videoPlayTimeMilli);
      std::int64_t seekTo = std::min(videoPlayTimeMilli + 10'000, durationMilli);
      seekFrame(seekTo);
      break;
    }
    case SDLK_LEFT: {
      spdlog::debug("Left Arrow pressed SDL {}ms, cur playtime: {}ms", curTime, videoPlayTimeMilli);
      std::int64_t seekTo = std::max(videoPlayTimeMilli - 10'000, 0ll);
      seekFrame(seekTo);
      break;
    }
    case SDLK_SPACE: {
      spdlog::debug("Space pressed SDL {}ms, cur playtime: {}ms", curTime, videoPlayTimeMilli);
      togglePause();
      break;
    }
    case SDLK_f: {
      spdlog::debug("Toggle full screen");
      if (!fullScreen) {
        SDL_SetWindowFullscreen(curWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
        fullScreen = true;
      } else {
        SDL_SetWindowFullscreen(curWindow, 0);
        fullScreen = false;
      }
      break;
    }
    case SDLK_ESCAPE: {
      if (fullScreen) {
        SDL_SetWindowFullscreen(curWindow, 0);
        fullScreen = false;
      }
    }
  };
}