//
// Created by delta on 1/22/2025.
//

#include "arcvp.h"


using namespace std::chrono;

void ArcVP::seekTo(std::int64_t milli){
	std::unique_lock fmt{fmtMtx,std::defer_lock};
	std::unique_lock video{audioMtx,std::defer_lock};
	std::unique_lock audio{videoMtx,std::defer_lock};

	std::lock(fmt,video,audio);

	spdlog::debug("seek to {}ms",milli);
	auto now=system_clock::now();
	auto played=duration_cast<milliseconds>(now-videoStart).count();
	if(videoCodecContext!=nullptr) {
		videoPacketQueue.clear();
		int64_t ts=timeToPts(milli,videoStream->time_base);
		spdlog::debug("vdeo pts: {}",ts);
		int ret=av_seek_frame(formatContext,videoStreamIndex,ts,AVSEEK_FLAG_BACKWARD);
		if(ret<0) {
			spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
			return;
		}
		avcodec_flush_buffers(videoCodecContext);
		videoPacketQueue.clear();
	}

	if(audioCodecContext!=nullptr) {
		audioPacketQueue.clear();
		int64_t ts=timeToPts(milli,audioStream->time_base);
		spdlog::debug("audio pts: {}",ts);

		int ret=av_seek_frame(formatContext,audioStreamIndex,ts,AVSEEK_FLAG_BACKWARD);
		if(ret<0) {
			spdlog::error("Unable to seek ts: {}, {}",ts,av_err2str(ret));
			return;
		}
		avcodec_flush_buffers(audioCodecContext);
		videoPacketQueue.clear();
	}
	if(!videoPacketQueue.empty()) {
		videoPacketQueue.clear();
	}
	if(!audioPacketQueue.empty()) {
		audioPacketQueue.clear();
	}
	AVPacket* pkt=av_packet_alloc();
	AVFrame*frame=av_frame_alloc();
	while (true) {
		int ret = avcodec_receive_frame(videoCodecContext, frame);;
		if (ret == 0) {
			auto t=ptsToTime(frame->pts,videoStream->time_base);
			spdlog::debug("t: {}, pts: {}",t,frame->pts);
			if(t>=milli) {
				break;
			}
			av_frame_unref(frame);
			continue;
		}
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			av_read_frame(formatContext,pkt);
			if (!pkt) {
				return;
			}
			if(pkt->stream_index==videoStreamIndex) {
				ret = avcodec_send_packet(videoCodecContext, pkt);
				if (ret < 0) {
					spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
				}
			}else if(pkt->stream_index==audioStreamIndex){
				ret = avcodec_send_packet(audioCodecContext, pkt);
				if (ret < 0) {
					spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
				}
			}
			av_packet_unref(pkt);
		}
		else {
			spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
			av_frame_free(&frame);
		}
	}
	audioBuffer.clear();
	audioPos=0;
	while (true) {
		int ret = avcodec_receive_frame(audioCodecContext, frame);;
		if (ret == 0) {
			auto t=ptsToTime(frame->pts,audioStream->time_base);
			spdlog::debug("t: {}, pts: {}",t,frame->pts);
			if(t>=milli) {
				break;
			}
			av_frame_unref(frame);
			continue;
		}
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			av_read_frame(formatContext,pkt);
			if (!pkt) {
				return;
			}
			if(pkt->stream_index==videoStreamIndex) {
				ret = avcodec_send_packet(videoCodecContext, pkt);
			}else if(pkt->stream_index==audioStreamIndex){
				ret = avcodec_send_packet(audioCodecContext, pkt);
			}
			if (ret < 0) {
				spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
			}
			av_packet_unref(pkt);
		}
		else {
			spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
			av_frame_free(&frame);
		}
	}
	av_frame_free(&frame);
	av_packet_free(&pkt);
	videoStart=now-milliseconds(static_cast<int>( milli/speed ));

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
