//
// Created by delta on 5/8/2025.
//
#pragma once
extern "C" {

#include <SDL3/SDL.h>
}
namespace ArcVP {

struct AudioDevice {
  SDL_AudioDeviceID id = -1;
  const char* name = nullptr;
  SDL_AudioSpec spec{};


  ~AudioDevice() {
    if (id!=-1) {
      SDL_CloseAudioDevice(id);
    }
  }
};
}  // namespace ArcVP
