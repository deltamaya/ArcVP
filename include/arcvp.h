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
#include "Channel.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

struct ClearAVPacket{
	void operator()(AVPacket* pkt) const{
		spdlog::debug("clear packet");
		av_packet_free(&pkt);
	}
};


class ArcVP{
	AVFormatContext* formatContext = nullptr;
	const AVCodec* videoCodec = nullptr;
	const AVCodec* audioCodec = nullptr;
	AVCodecContext* videoCodecContext = nullptr;
	AVCodecContext* audioCodecContext = nullptr;
	const AVCodecParameters* videoCodecParams = nullptr;
	const AVCodecParameters* audioCodecParams = nullptr;
	std::unique_ptr<std::thread> decodeProducer = nullptr, decodeConsumer = nullptr;
	Channel<AVPacket *, ClearAVPacket> videoPacketQueue, audioPacketQueue;

	const AVStream *videoStream = nullptr, *audioStream = nullptr;
	int videoStreamIndex = -1, audioStreamIndex = -1;

	std::atomic<bool> running, pause, finished;
	std::mutex mtx{};
	AVRational timebase{};

	SDL_AudioDeviceID audioDeviceID = -1;
	char* audioDeviceName = nullptr;
	SDL_AudioSpec audioSpec{};


	int width = -1, height = -1;
	int durationMilli = -1;

	void decodeProduceThreadBody();

	void decodeConsumeThreadBody();

	bool setupAudioDevice(int);

public:
	bool resampleAudioFrame(AVFrame* frame, std::vector<uint8_t>&vec);

	friend void audioCallback(void* userdata, Uint8* stream, int len);

	ArcVP(){
		running.store(true);
	}

	~ArcVP(){
		running.store(false);

		videoPacketQueue.close();
		audioPacketQueue.close();
		if (decodeProducer) {
			decodeProducer->join();
		}
		if (decodeConsumer) {
			decodeConsumer->join();
		}
	}

	bool open(const char*);

	void close();

	void startPlayback();

	void togglePause(){
		bool p = pause.load();
		pause.store(!p);
	}

	void seekTo(std::int64_t);

	void speedUp();

	void speedDown();

	std::tuple<int, int> getWH(){
		return std::make_tuple(width, height);
	}

	AVRational getTimebase(){
		return timebase;
	}
};

enum ArcVPEvent{
	ARCVP_NEXTFRAME_EVENT = SDL_USEREVENT + 1,
	ARCVP_FINISH_EVENT,
};


#endif //ARCVP_H
