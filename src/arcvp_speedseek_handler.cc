//
// Created by delta on 1/22/2025.
//

#include "arcvp.h"


using namespace std::chrono;

void ArcVP::seekTo(std::int64_t milli){
	std::scoped_lock lk{fmtMtx,videoMtx,audioMtx,renderQueueMtx,audioQueueMtx};

	spdlog::debug("seek to {}s",milli/1000.);
	int64_t elapsed=duration_cast<milliseconds>(system_clock::now()-videoStart).count();

	videoPacketQueue.clear();
	audioPacketQueue.clear();
	spdlog::debug("render queue before seek size: {}",renderQueue.size());

	while (!renderQueue.empty()&&renderQueue.front().presentTimeMs<milli) {
		renderQueueReady.acquire();
		renderQueue.pop_front();
		renderQueueEmpty.release();
	}

	spdlog::debug("render queue after seek size: {}",renderQueue.size());

	spdlog::debug("audio queue before seek size: {}",audioQueue.size());

	while (!audioQueue.empty()&&audioQueue.front().presentTimeMs<milli) {
		audioQueueReady.acquire();
		audioQueue.pop_front();
		audioQueueEmpty.release();
	}
	spdlog::debug("audio queue after seek size: {}",audioQueue.size());

	videoStart=system_clock::now()-milliseconds(milli);

	if(videoCodecContext!=nullptr) {
		int64_t ts=timeToPts(milli,videoStream->time_base);
		spdlog::debug("video pts: {}",ts);
		int ret=av_seek_frame(formatContext,videoStreamIndex,ts,AVSEEK_FLAG_BACKWARD);
		if(ret<0) {
			spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
			return;
		}
		avcodec_flush_buffers(videoCodecContext);
	}

	if(audioCodecContext!=nullptr) {
		int64_t ts=timeToPts(milli,audioStream->time_base);
		spdlog::debug("audio pts: {}",ts);

		int ret=av_seek_frame(formatContext,audioStreamIndex,ts,AVSEEK_FLAG_BACKWARD);
		if(ret<0) {
			spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
			return;
		}
		avcodec_flush_buffers(audioCodecContext);
	}
}

void ArcVP::speedUp(){
    std::unique_lock lk{videoMtx};
	if(speed<1) {
		speed=1;
	}else if(speed<1.5){
		speed=1.5;
	}else {
		speed=2;
	}
	auto pTimeMilli=ptsToTime(prevFramePts,videoStream->time_base);
	bool p=pause.load();
	if (audioDeviceID > 0) {
		SDL_CloseAudioDevice(audioDeviceID);
	}
	if(!p) {
		togglePause();
		setupAudioDevice(audioCodecParams->sample_rate*speed);
		togglePause();
	}else {
		setupAudioDevice(audioCodecParams->sample_rate*speed);
	}



	videoStart = system_clock::now() - milliseconds(static_cast<int>( pTimeMilli / speed ));
}

void ArcVP::speedDown(){
	std::unique_lock lk{videoMtx};
	if(speed>1.5) {
		speed=1.5;
	}else if(speed>1) {
		speed=1;
	}else {
		speed=0.5;
	}
	bool p=pause.load();
	if (audioDeviceID > 0) {
		SDL_CloseAudioDevice(audioDeviceID);
	}
	if(!p) {
		togglePause();
		setupAudioDevice(audioCodecParams->sample_rate*speed);
		togglePause();
	}else {
		setupAudioDevice(audioCodecParams->sample_rate*speed);
	}

	auto pTimeMilli=ptsToTime(prevFramePts,videoStream->time_base);
	videoStart = system_clock::now() - milliseconds(static_cast<int>( pTimeMilli / speed ));
}
