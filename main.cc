
#include <SDL3_ttf/SDL_ttf.h>

#include <iostream>

#include "player.h"

using namespace std::chrono;

typedef struct VideoState {
  int src_width;           // 原始视频宽度
  int src_height;          // 原始视频高度
  int window_width;        // 窗口宽度
  int window_height;       // 窗口高度
  SDL_FRect display_rect;  // 显示区域
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
ArcVP::Player* arc = ArcVP::Player::instance();

void presentFrame(AVFrame* frame) {
  auto [width, height] = arc->getWH();


  // SDL_RenderClear(renderer);
  // SDL_RenderTexture(renderer, videoTexture, nullptr, &state.display_rect);
  // SDL_RenderPresent(renderer);
}

void handleKeyDown(SDL_Window* window, ArcVP::Player* arc,
                   const SDL_Event& event) {
  static bool fullscreen = false;
  int64_t t = -1;
  switch (event.key.key) {
    case SDLK_SPACE:
      arc->togglePause();
      break;
    case SDLK_F:
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

void handle_event(SDL_Event const& event) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      arc->sync_state_.should_exit = true;
      break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      handleResize();
      break;
    case SDL_EVENT_KEY_DOWN:
      handleKeyDown(window, arc, event);
      break;
    default:
      break;
  }
}

int main() {
  spdlog::set_level(spdlog::level::debug);

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
    spdlog::error("SDL_Init: {}", SDL_GetError());
    return 1;
  }

  if (!TTF_Init()) {
    spdlog::error("TTF_Init: {}", SDL_GetError());
    return 1;
  }

  window = SDL_CreateWindow(
      "Arc VP", state.window_width, state.window_height,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);

  if (window == nullptr) {
    printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
    return -1;
  }

  renderer = SDL_CreateRenderer(window, nullptr);
  SDL_SetRenderVSync(renderer, 1);
  if (renderer == nullptr) {
    SDL_Log("Error: SDL_CreateRenderer(): %s\n", SDL_GetError());
    return -1;
  }
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  ImGui::StyleColorsDark();
  // Setup Platform/Renderer backends
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  constexpr int fontSize = 50;

  TTF_Font* font = TTF_OpenFont("C:/Windows/Fonts/CascadiaCode.ttf", fontSize);
  if (!font) {
    spdlog::error("Failed to load font: {}\n", SDL_GetError());
    return 1;
  }
  std::string text = "Hello, SDL!";
  SDL_Color textColor = {255, 255, 255, 0};
  SDL_Surface* surface =
      TTF_RenderText_Blended(font, text.c_str(), 0, textColor);

  arc->open("test.mp4");

  auto [width, height] = arc->getWH();
  spdlog::info("w: {}, h: {}", width, height);
  state.src_width = width;
  state.src_height = height;

  SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, surface);
  videoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12,
                                   SDL_TEXTUREACCESS_STREAMING, state.src_width,
                                   state.src_height);

  SDL_Rect dstRect;
  dstRect.x = 50;
  dstRect.y = 50;
  dstRect.w = surface->w;
  dstRect.h = surface->h;

  handleResize();
  arc->startPlayback();

  SDL_Event event;
  while (!arc->sync_state_.should_exit) {
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      handle_event(event);
    }
    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Arc VP");
    if (!arc->sync_state_.pause) {
      auto frame = arc->getVideoFrame();
      if (frame) {
        SDL_UpdateYUVTexture(videoTexture, nullptr, frame->data[0],
                     frame->linesize[0],                   // Y plane
                     frame->data[1], frame->linesize[1],   // U plane
                     frame->data[2], frame->linesize[2]);  // V plane
        av_frame_free(&frame);
      }
    }

    ImGui::Image((ImTextureID)videoTexture, ImVec2(state.window_width,state.window_height));
    ImGui::End();
    ImGui::Render();
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(),renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(10);
  }

  arc->exit();
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  SDL_DestroyTexture(textTexture);
  TTF_CloseFont(font);
  TTF_Quit();
  SDL_Quit();
  return 0;
}
