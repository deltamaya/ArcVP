//
// Created by delta on 1/22/2025.
//

#ifndef ARCVP_H
#define ARCVP_H

#include <spdlog/spdlog.h>
#include <cstdint>
#include <vector>
#include <optional>
#include <spdlog/spdlog.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

class ArcVP{
	AVFormatContext* formatContext = nullptr;
	const AVCodec* videoCodec = nullptr;
	const AVCodec* audioCodec = nullptr;
	AVCodecContext* videoCodecContext = nullptr;
	AVCodecContext* audioCodecContext = nullptr;
	const AVCodecParameters* videoCodecParams = nullptr;
	const AVCodecParameters* audioCodecParams = nullptr;
	std::unique_ptr<std::thread> decodeThread = nullptr, playbackThread = nullptr;
	std::queue<AVPacket *> videoPacketQueue, audioPacketQueue;

	const AVStream *videoStream = nullptr, *audioStream = nullptr;
	int videoStreamIndex = -1, audioStreamIndex = -1;


	std::vector<std::uint8_t>displayBuffer,idleBuffer;
	std::mutex mtx;


	int width=-1,height=-1;
	int durationMilli=-1;

	void decodeThreadBody();
	void playbackThreadBody();

	void setupDecodeThread();

	void setupPlaybackThread();

public:
	bool open(const char*);

	void close();

	void startPlayback();

	void pause();

	void seekTo(std::int64_t);

	void speedUp();

	void speedDown();

	std::tuple<int,int> getWH(){
		return std::make_tuple(width,height);
	}
};

enum ArcVPEvent{
	ARCVP_NEXTFRAME_EVENT=SDL_USEREVENT+1,
};


#endif //ARCVP_H
