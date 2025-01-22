#include "arcvp.h"
#include <SDL_ttf.h>

int main(){

	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		spdlog::error("SDL_Init: {}", SDL_GetError());
		return 1;
	}

	if (TTF_Init() == -1) {
		spdlog::error("TTF_Init: {}", TTF_GetError());
		return 1;
	}

	SDL_Window* window=SDL_CreateWindow("Arc VP", SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,640,480,0);

	SDL_Renderer*renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	constexpr int fontSize=50;

	TTF_Font* font=TTF_OpenFont("C:/Windows/Fonts/CascadiaCode.ttf",fontSize);
	if (!font) {
		spdlog::error("Failed to load font: {}\n", TTF_GetError());
		return 1;
	}
	std::string text="Hello, SDL!";
	SDL_Color textColor={255,255,255,0};
	SDL_Surface* surface = TTF_RenderText_Blended(font, text.c_str(), textColor);
	SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);

	ArcVP arcvp;
	arcvp.open("test.mp4");
	bool running=true;

	SDL_Rect dstRect;
	dstRect.x = 50;
	dstRect.y = 50;
	dstRect.w = surface->w;
	dstRect.h = surface->h;

	while(running) {
		SDL_Event event;
		while(SDL_PollEvent(&event)) {
			if(event.type==SDL_QUIT) {
				running=false;
			}
			SDL_RenderClear(renderer);
			SDL_RenderCopy(renderer, texture, nullptr, &dstRect);
			SDL_RenderPresent(renderer);
		}
	}


	SDL_DestroyTexture(texture);
	TTF_CloseFont(font);
	TTF_Quit();
	SDL_Quit();
	return 0;
}