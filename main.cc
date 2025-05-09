#include <SDL_ttf.h>

#include <iostream>

#include "player.h"

using namespace std::chrono;

typedef struct VideoState {
  int src_width;          // 原始视频宽度
  int src_height;         // 原始视频高度
  int window_width;       // 窗口宽度
  int window_height;      // 窗口高度
  SDL_Rect display_rect;  // 显示区域
} VideoState;

VideoState state = {.window_width = 1280, .window_height = 720};

// 计算保持宽高比的显示区域
void calculateDisplayRect() {
  float aspect_ratio = (float)state.src_width / state.src_height;
  float window_aspect_ratio = (float)state.window_width / state.window_height;

  if (aspect_ratio > window_aspect_ratio) {
    // 视频比窗口更宽，以窗口宽度为基准
    state.display_rect.w = state.window_width;
    state.display_rect.h = (int)(state.window_width / aspect_ratio);
    state.display_rect.x = 0;
    state.display_rect.y = (state.window_height - state.display_rect.h) / 2;
  } else {
    // 视频比窗口更高，以窗口高度为基准
    state.display_rect.h = state.window_height;
    state.display_rect.w = (int)(state.window_height * aspect_ratio);
    state.display_rect.x = (state.window_width - state.display_rect.w) / 2;
    state.display_rect.y = 0;
  }
}

SDL_Window* window = nullptr;

SDL_Renderer* renderer = nullptr;

SDL_Texture* videoTexture = nullptr;

AVFrame* frame = nullptr;

void handleResize() {
  SDL_GetWindowSize(window, &state.window_width, &state.window_height);
  calculateDisplayRect();
}

void presentFrame(AVFrame* frame) {
  SDL_UpdateYUVTexture(videoTexture, nullptr, frame->data[0],
                       frame->linesize[0],                   // Y plane
                       frame->data[1], frame->linesize[1],   // U plane
                       frame->data[2], frame->linesize[2]);  // V plane
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, videoTexture, nullptr, &state.display_rect);
  SDL_RenderPresent(renderer);
}

void handleKeyDown(SDL_Window* window, ArcVP::Player* arc,
                   const SDL_Event& event) {
  static bool fullscreen = false;
  int64_t t = -1;
  switch (event.key.keysym.sym) {
    // case SDLK_SPACE:
    //   arc.togglePause();
    //   break;
    case SDLK_f:
      fullscreen = !fullscreen;
      SDL_SetWindowFullscreen(window, fullscreen);
      handleResize();
      presentFrame(frame);
      break;
    case SDLK_LEFT:
      t = arc->getPlayedMs();
      arc->seekTo(std::max(t - 5000, 0ll));
      break;
    case SDLK_RIGHT:
      t = arc->getPlayedMs();
      arc->seekTo(t + 5000);
      break;
    // case SDLK_UP:
    //   arc.speedUp();
    //   break;
    // case SDLK_DOWN:
    //   arc.speedDown();
    //   break;
    default:
      break;
  }
}

ArcVP::Player* arc=ArcVP::Player::instance();

int main() {
  spdlog::set_level(spdlog::level::debug);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
    spdlog::error("SDL_Init: {}", SDL_GetError());
    return 1;
  }

  if (TTF_Init() == -1) {
    spdlog::error("TTF_Init: {}", TTF_GetError());
    return 1;
  }

  window = SDL_CreateWindow("Arc VP", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, state.window_width,
                            state.window_height, SDL_WINDOW_RESIZABLE);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  constexpr int fontSize = 50;

  TTF_Font* font = TTF_OpenFont("C:/Windows/Fonts/CascadiaCode.ttf", fontSize);
  if (!font) {
    spdlog::error("Failed to load font: {}\n", TTF_GetError());
    return 1;
  }
  std::string text = "Hello, SDL!";
  SDL_Color textColor = {255, 255, 255, 0};
  SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), textColor);

  arc->open("test.mp4");

  auto [width, height] = arc->getWH();
  spdlog::info("w: {}, h: {}", width, height);
  state.src_width = width;
  state.src_height = height;

  SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, surface);
  videoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                   SDL_TEXTUREACCESS_STREAMING, state.src_width,
                                   state.src_height);

  bool running = true;

  SDL_Rect dstRect;
  dstRect.x = 50;
  dstRect.y = 50;
  dstRect.w = surface->w;
  dstRect.h = surface->h;

  handleResize();

  arc->startPlayback();

  SDL_Event event;
  while (running) {
    {
      AVFrame* frame = arc->tryFetchVideoFrame();
      if (frame) {
        presentFrame(frame);
        av_frame_free(&frame);
      }
    }
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          running = false;
          break;
        case SDL_WINDOWEVENT:
          if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
            handleResize();
            presentFrame(frame);
          } else if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            handleResize();
            presentFrame(frame);
          }
          break;
        case SDL_KEYDOWN:
          handleKeyDown(window, arc, event);
          break;
        // case ARCVP_NEXTFRAME_EVENT:
        // 	// if (event.type == ARCVP_NEXTFRAME_EVENT) {
        // 	// 	av_frame_free(&frame);
        // 	// 	frame = static_cast<AVFrame *>( event.user.data1 );
        // 	// 	// spdlog::debug("frame pointer: {}",(void*)frame);
        // 	// }
        // 	break;
        // case ARCVP_FINISH_EVENT:
        // 	spdlog::debug("play finish");
        // 	break;
        default:
          break;
      }
    }
    SDL_Delay(10);
  }
  arc->exit();

  SDL_DestroyTexture(textTexture);
  TTF_CloseFont(font);
  TTF_Quit();
  SDL_Quit();
  return 0;
}
