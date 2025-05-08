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

struct DisposeAVPacket{
	void operator()(AVPacket* pkt) const{
		av_packet_free(&pkt);
	}
};

struct DisposeAVFrame{
	void operator()(AVFrame* frame)const{
		av_frame_free(&frame);
	}
};


int64_t ptsToTime(int64_t pts, AVRational timebase);

int64_t timeToPts(int64_t milli, AVRational timebase);
enum ArcVPEvent{
	ARCVP_NEXTFRAME_EVENT = SDL_USEREVENT + 1,
	ARCVP_FINISH_EVENT,
};

class ArcVP{
	enum class WorkerStatus{
		Working,
		Idle,
		Exiting
	};
	AVFormatContext* formatContext = nullptr;
	const AVCodec* videoCodec = nullptr;
	const AVCodec* audioCodec = nullptr;
	AVCodecContext* videoCodecContext = nullptr;
	AVCodecContext* audioCodecContext = nullptr;
	const AVCodecParameters* videoCodecParams = nullptr;
	const AVCodecParameters* audioCodecParams = nullptr;
	std::unique_ptr<std::thread> decoderThread = nullptr, playbackThread = nullptr,audioDecodeThread=nullptr;
	Channel<AVPacket *,256, DisposeAVPacket> videoPacketQueue, audioPacketQueue;

	const AVStream *videoStream = nullptr, *audioStream = nullptr;
	int videoStreamIndex = -1, audioStreamIndex = -1;

	std::atomic_bool running,pause;
	std::atomic<WorkerStatus> decoderWorkerStatus=WorkerStatus::Idle,playbackWorkerStatus=WorkerStatus::Idle,audioDecoderWorkerStatus=WorkerStatus::Idle;

	std::mutex fmtMtx{}, videoMtx{}, audioMtx{},renderQueueMtx{},audioQueueMtx{};

	SDL_AudioDeviceID audioDeviceID = -1;
	char* audioDeviceName = nullptr;
	SDL_AudioSpec audioSpec{};

	std::vector<uint8_t> audioBuffer;


	int width = -1, height = -1;
	std::chrono::system_clock::time_point videoStart;

	int64_t audioPos = 0, prevFramePts = AV_NOPTS_VALUE;
	double speed = 1.;
	struct RenderEntry{
		AVFrame* frame;
		int64_t presentTimeMs;
	};

	std::deque<RenderEntry> renderQueue;
	std::deque<RenderEntry> audioQueue;
	std::counting_semaphore<> renderQueueReady,renderQueueEmpty,audioQueueReady,audioQueueEmpty;


	void decoderWorker();

	void playbackWorker();

	void audioDecodeWorker();

	bool setupAudioDevice(int);

public:
	bool resampleAudioFrame(AVFrame* frame);

	friend void audioCallback(void* userdata, Uint8* stream, int len);

	ArcVP(): renderQueueReady(0),renderQueueEmpty(400),audioQueueReady(0),audioQueueEmpty(500){
		running = true;
	}

	~ArcVP(){
		running=false;

		decoderWorkerStatus=WorkerStatus::Exiting;
		playbackWorkerStatus=WorkerStatus::Exiting;
		audioDecoderWorkerStatus=WorkerStatus::Exiting;

		videoPacketQueue.close();
		audioPacketQueue.close();

		{
			std::scoped_lock lk{renderQueueMtx};
			while (!renderQueue.empty()) {
				renderQueueReady.acquire();
				renderQueue.pop_front();
				renderQueueEmpty.release();
			}
		}

		{
			std::scoped_lock lk{audioQueueMtx};
			while (!audioQueue.empty()) {
				audioQueueReady.acquire();
				audioQueue.pop_front();
				audioQueueEmpty.release();
			}
		}


		if (decoderThread) {
			decoderThread->join();
		}
		if (playbackThread) {
			playbackThread->join();
		}
		if (audioDecodeThread) {
			audioDecodeThread->join();
		}
	}

	AVFrame* tryFetchFrame(std::chrono::system_clock::time_point tp){
		auto milli=duration_cast<std::chrono::milliseconds>(tp-videoStart).count();
		bool ok=renderQueueReady.try_acquire();
		if (!ok) {
			return nullptr;
		}
		std::scoped_lock lk{renderQueueMtx};
		auto front=renderQueue.front();
		if (front.presentTimeMs<milli) {
			renderQueueEmpty.release();
			renderQueue.pop_front();
			return front.frame;
		}
		renderQueueReady.release();
		return nullptr;
	}

	AVFrame* tryFetchAudioFrame(){
		auto milli=duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-videoStart).count();
		bool ok=audioQueueReady.try_acquire();
		if (!ok) {
			return nullptr;
		}
		std::scoped_lock lk{audioQueueMtx};

		auto front=audioQueue.front();
		spdlog::debug("elapsed: {}, calc: {}",milli/1000.,front.presentTimeMs/1000.);

		if (front.presentTimeMs<milli) {
			audioQueueEmpty.release();
			audioQueue.pop_front();
			return front.frame;
		}
		audioQueueReady.release();
		return nullptr;
	}

	bool open(const char*);

	void close();

	void startPlayback();

	void togglePause(){
		pause=!pause;
		SDL_PauseAudioDevice(audioDeviceID, pause);
		if (!pause) {
			auto t = ptsToTime(prevFramePts, videoStream->time_base);
			videoStart = std::chrono::system_clock::now() - std::chrono::milliseconds(static_cast<int>( t / speed ));
		}
	}

	void seekTo(std::int64_t milli);

	void speedUp();

	void speedDown();


	void audioSyncTo(AVFrame* frame);

	std::int64_t getAudioPlayTime(std::int64_t bytesPlayed);

	std::tuple<int, int> getWH(){
		return std::make_tuple(width, height);
	}

	int64_t getPlayDuration(){
		std::unique_lock lk{videoMtx};
		return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - videoStart).
				count();
	}
};


using namespace std::chrono;

inline int64_t ptsToTime(int64_t pts, AVRational timebase){
	return pts * 1000. * timebase.num / timebase.den;
}

inline int64_t timeToPts(int64_t milli, AVRational timebase){
	return milli / 1000. * timebase.den / timebase.num;
}

#endif //ARCVP_H
