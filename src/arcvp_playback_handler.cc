//
// Created by delta on 1/22/2025.
//


#include "arcvp.h"


void pushNextFrameEvent(void*data){
	SDL_Event event;
	event.type = ARCVP_NEXTFRAME_EVENT;
	event.user.data1 = data;
	SDL_PushEvent(&event);
}


void ArcVP::startPlayback(){
	// demux all packets from the format context
	while (true) {
		AVPacket* pkt = av_packet_alloc();
		if (av_read_frame(formatContext, pkt) == 0) {
			if (pkt->stream_index == videoStreamIndex) {
				videoPacketQueue.push(pkt);
			}
			else if (pkt->stream_index == audioStreamIndex) {
				audioPacketQueue.push(pkt);
			}
			else {
				spdlog::warn("Unknown packet type: {}", pkt->stream_index);
			}
		}
		else {
			av_packet_free(&pkt);
			break;
		}
	}
	spdlog::debug("video packet queue size: {}", videoPacketQueue.size());
	spdlog::debug("audio packet queue size: {}", audioPacketQueue.size());


	if (!decodeThread) {
		decodeThread = std::make_unique<std::thread>([this]{this->decodeThreadBody();});
	}
}

bool tryReceiveFrame(AVCodecContext*ctx,AVFrame*frame){
	int ret=avcodec_receive_frame(ctx,frame);
	if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
		return false;
	}
	if (ret < 0) {
		spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
		return false;
	}
	return true;
}

void ArcVP::decodeThreadBody(){
	AVFrame* frame =av_frame_alloc();
	while (!tryReceiveFrame(videoCodecContext,frame)) {
		auto pkt = videoPacketQueue.front();
		videoPacketQueue.pop();
		avcodec_send_packet(videoCodecContext, pkt);
		av_packet_unref(pkt);
	}
	// SwsContext* swsContext = sws_getContext(frame->width, frame->height, static_cast<enum AVPixelFormat>(frame->format), width, height, AV_PIX_FMT_YUV420P,
	//                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
	// idleBuffer.resize(width*height*4);
	// displayBuffer.resize(width*height*4);
	// uint8_t* dest[1]={this->idleBuffer.data()};
	// int dest_linesize[1]={width*3};
	//
	// sws_scale(swsContext,frame->data,frame->linesize,0,height,dest,dest_linesize);

	pushNextFrameEvent(frame);
}
