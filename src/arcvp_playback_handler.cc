//
// Created by delta on 1/22/2025.
//


#include "arcvp.h"

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}


void pushFinishEvent(){
	SDL_Event event;
	event.type = ARCVP_FINISH_EVENT;
	SDL_PushEvent(&event);
}


void ArcVP::startPlayback(){
	SDL_GetDefaultAudioInfo(&audioDeviceName, &audioSpec, false);
	spdlog::info("default audio device: {}", audioDeviceName);
	if (!setupAudioDevice(audioCodecContext->sample_rate)) {
		spdlog::info("Unable to setup audio device: {}", audioDeviceName);
		return;
	}
	videoStart = system_clock::now();

	if (!decoderThread) {
		decoderThread = std::make_unique<std::thread>([this]{ this->decoderWorker(); });
	}
	if (!playbackThread) {
		playbackThread = std::make_unique<std::thread>([this]{ this->playbackWorker(); });
	}
	if (!audioDecodeThread) {
		audioDecodeThread = std::make_unique<std::thread>([this]{ this->audioDecodeWorker(); });
	}
	decoderWorkerStatus = WorkerStatus::Working;
	playbackWorkerStatus = WorkerStatus::Working;
	audioDecoderWorkerStatus=WorkerStatus::Working;
	SDL_PauseAudioDevice(audioDeviceID, false);
}

void ArcVP::decoderWorker(){
	// demux all packets from the format context
	while (running) {
		while (decoderWorkerStatus == WorkerStatus::Idle) {
			std::this_thread::sleep_for(10ms);
		}
		if (decoderWorkerStatus == WorkerStatus::Exiting) {
			break;
		}
		AVPacket* pkt = av_packet_alloc();
		if (!pkt) {
			spdlog::error("Fail to allocate AVPacket");
			std::exit(1);
		}
		int ret; {
			std::scoped_lock lk{fmtMtx};
			ret = av_read_frame(formatContext, pkt);
		}
		if (ret < 0) {
			av_packet_free(&pkt);
			if (ret == AVERROR_EOF) {
				decoderWorkerStatus = WorkerStatus::Idle;
				continue;
			}
			spdlog::error("Error reading frame: {}", av_err2str(ret));
			std::exit(1);
		}
		if (pkt->stream_index == videoStreamIndex) {
			videoPacketQueue.send(pkt);
		}
		else if (pkt->stream_index == audioStreamIndex) {
			audioPacketQueue.send(pkt);
		}
		else {
			spdlog::warn("Unknown packet index: {}", pkt->stream_index);
			av_packet_free(&pkt);
		}
	}
	spdlog::info("Decoder Thread Exited");
}

