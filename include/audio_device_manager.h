//
// Created by delta on 5/8/2025.
//
#pragma once

namespace ArcVP {
extern "C" {

#include <SDL2/SDL.h>
}
struct AudioDeviceManager {
  SDL_AudioDeviceID audio_device_id = -1;
  char* audio_device_name = nullptr;
  SDL_AudioSpec audio_spec{};
};
}  // namespace ArcVP
