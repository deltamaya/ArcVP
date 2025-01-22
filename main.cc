#include "arcvp.h"
#include <SDL_ttf.h>

using namespace std::chrono;

typedef struct VideoState{
	int src_width; // 原始视频宽度
	int src_height; // 原始视频高度
	int window_width; // 窗口宽度
	int window_height; // 窗口高度
	SDL_Rect display_rect; // 显示区域
} VideoState;

// 计算保持宽高比的显示区域
void calculateDisplayRect(VideoState* state){
	float aspect_ratio = ( float )state->src_width / state->src_height;
	float window_aspect_ratio = ( float )state->window_width / state->window_height;

	if (aspect_ratio > window_aspect_ratio) {
		// 视频比窗口更宽，以窗口宽度为基准
		state->display_rect.w = state->window_width;
		state->display_rect.h = ( int )(state->window_width / aspect_ratio);
		state->display_rect.x = 0;
		state->display_rect.y = (state->window_height - state->display_rect.h) / 2;
	}
	else {
		// 视频比窗口更高，以窗口高度为基准
		state->display_rect.h = state->window_height;
		state->display_rect.w = ( int )(state->window_height * aspect_ratio);
		state->display_rect.x = (state->window_width - state->display_rect.w) / 2;
		state->display_rect.y = 0;
	}
}

// 窗口大小改变时的处理
void handleResize(SDL_Window* window, VideoState* state){
	SDL_GetWindowSize(window, &state->window_width, &state->window_height);
	calculateDisplayRect(state);
}

int main(){
	spdlog::set_level(spdlog::level::debug);

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		spdlog::error("SDL_Init: {}", SDL_GetError());
		return 1;
	}

	if (TTF_Init() == -1) {
		spdlog::error("TTF_Init: {}", TTF_GetError());
		return 1;
	}

	VideoState state = {
		.src_width = 1920, // 原始视频宽度
		.src_height = 1080, // 原始视频高度
		.window_width = 1280, // 初始窗口宽度
		.window_height = 720 // 初始窗口高度
	};

	SDL_Window* window = SDL_CreateWindow("Arc VP", SDL_WINDOWPOS_CENTERED,
	                                      SDL_WINDOWPOS_CENTERED, state.window_width, state.window_height,
	                                      SDL_WINDOW_RESIZABLE);

	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
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

	ArcVP arcvp;
	arcvp.open("test.mp4");
	auto [width,height] = arcvp.getWH();
	spdlog::info("w: {}, h: {}", width, height);
	state.src_width = width;
	state.src_height = height;

	SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_Texture* videoTexture =
			SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, state.src_width,
			                  state.src_height);

	bool running = true;

	SDL_Rect dstRect;
	dstRect.x = 50;
	dstRect.y = 50;
	dstRect.w = surface->w;
	dstRect.h = surface->h;



	handleResize(window,&state);

	arcvp.startPlayback();



	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					running = false;
					break;
				case SDL_WINDOWEVENT:
					if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
						handleResize(window, &state);
					}
					break;
				case ARCVP_NEXTFRAME_EVENT:
					if (event.type == ARCVP_NEXTFRAME_EVENT) {
						spdlog::debug("User event");
						auto frame = static_cast<AVFrame *>( event.user.data1 );
						SDL_UpdateYUVTexture(videoTexture, nullptr,
						                     frame->data[0], frame->linesize[0], // Y plane
						                     frame->data[1], frame->linesize[1], // U plane
						                     frame->data[2], frame->linesize[2]); // V plane
						SDL_RenderClear(renderer);
						// 使用计算好的显示区域进行渲染
						SDL_RenderCopy(renderer, videoTexture, nullptr, &state.display_rect);
						SDL_RenderPresent(renderer);
					}
					break;
				default: ;
			}
		}
		SDL_Delay(24);
	}


	SDL_DestroyTexture(textTexture);
	TTF_CloseFont(font);
	TTF_Quit();
	SDL_Quit();
	return 0;
}
