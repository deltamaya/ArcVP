//
// Created by delta on 1/22/2025.
//


#include "arcvp.h"


using namespace std::chrono;

void pushNextFrameEvent(void* data){
	SDL_Event event;
	event.type = ARCVP_NEXTFRAME_EVENT;
	event.user.data1 = data;
	SDL_PushEvent(&event);
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
	// demux all packets from the format context
	while (true) {
		AVPacket* pkt = av_packet_alloc();
		if (!pkt) {
			spdlog::error("Failed to allocate AVPacket");
			break;
		}

		int ret = av_read_frame(formatContext, pkt);
		if (ret < 0) {
			av_packet_free(&pkt);
			if (ret == AVERROR_EOF) {
				break;
			} else {
				spdlog::error("Error reading frame: {}", av_err2str(ret));
				break;
			}
		}

		if (pkt->stream_index == videoStreamIndex) {
			videoPacketQueue.push(pkt);
		} else if (pkt->stream_index == audioStreamIndex) {
			audioPacketQueue.push(pkt);
		} else {
			spdlog::warn("Unknown packet index: {}", pkt->stream_index);
			av_packet_free(&pkt);
		}
	}

	spdlog::debug("video packet queue size: {}", videoPacketQueue.size());
	spdlog::debug("audio packet queue size: {}", audioPacketQueue.size());


	if (!decodeThread) {
		decodeThread = std::make_unique<std::thread>([this]{ this->decodeThreadBody(); });
	}
}

void ArcVP::decodeThreadBody(){
	auto start = system_clock::now();
	auto timebase = this->getTimebase();
	int cnt=0;
	while (this->running.load()) {
		AVFrame* frame = av_frame_alloc();
		while(true) {
			int ret=avcodec_receive_frame(videoCodecContext, frame);;
			if(ret==AVERROR(EAGAIN)) {
				if(videoPacketQueue.empty()) {
					spdlog::debug("decode EOF");
					pushFinishEvent();
					return;
				}
				auto pkt = videoPacketQueue.front();
				videoPacketQueue.pop();
				ret=avcodec_send_packet(videoCodecContext, pkt);
				if(ret<0) {
					spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
				}
				av_packet_free(&pkt);
			}else if(ret==AVERROR_EOF) {
				pushFinishEvent();
				av_frame_free(&frame);
				spdlog::debug("decode EOF");
				return;
			}else if(ret==0) {
				break;
			}else {
				spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
				av_frame_free(&frame);
			}
		}
		int pTimeMilli = frame->pts * timebase.num * 1000. / timebase.den;

		while(pause.load()&&running.load()) {
			start = system_clock::now()-milliseconds(pTimeMilli);
			std::this_thread::sleep_for(10ms);
		}

		auto pTime = start + milliseconds(pTimeMilli);
		std::this_thread::sleep_until(pTime);
		pushNextFrameEvent(frame);
	}
}


bool ArcVP::resampleAudioFrame(AVFrame*frame,std::vector<uint8_t>&vec) {
	static SwrContext*ctx=nullptr;
	if (!ctx) {
		swr_alloc_set_opts2(&ctx,
							&audioCodecParams->ch_layout,
							AV_SAMPLE_FMT_FLT,
							audioCodecParams->sample_rate,
							&audioCodecParams->ch_layout,
							audioCodecContext->sample_fmt,
							audioCodecParams->sample_rate,
							0,
							nullptr);

		swr_init(ctx);
	}
	uint8_t **converted_data = nullptr;
	int max_samples = audioCodecContext->frame_size;
	av_samples_alloc_array_and_samples(&converted_data, nullptr, 2, max_samples, AV_SAMPLE_FMT_FLT, 0);
	if (!converted_data) {
		spdlog::error("Unable to alloc sample array");
		std::exit(1);
	}
	int outSamples = av_rescale_rnd(swr_get_delay(ctx, audioCodecContext->sample_rate) +
									frame->nb_samples,
									audioCodecContext->sample_rate,
									audioCodecContext->sample_rate,
									AV_ROUND_UP);

	vec.resize(outSamples * audioCodecContext->ch_layout.nb_channels * sizeof(float));
	uint8_t *outBuffer = vec.data();
	swr_convert(ctx,
				&outBuffer,
				outSamples,
				frame->data,
				frame->nb_samples);
	return true;
}

void audioCallback(void* userdata, Uint8* stream, int len){
	auto arc = static_cast<ArcVP *>( userdata );
	static std::int64_t bytesPlayed = 0;
	static std::int64_t pos = 0;
	static std::vector<uint8_t> audioBuffer;
	static AVFrame* frame = av_frame_alloc();
	//    vr->audioSyncTo(bytesPlayed);

	// paused
	if(arc->pause.load()) {
		memset(stream,0,len);
		return;
	}

	while (len > 0) {
		int bytesCopied = 0;
		if (pos >= audioBuffer.size()) {
			/* We have already sent all our data; get more */
			while(true) {
				int ret=avcodec_receive_frame(arc->audioCodecContext, frame);;
				if(ret==AVERROR(EAGAIN)) {
					if(arc->audioPacketQueue.empty()) {
						memset(stream,0,len);
						return;
					}
					auto pkt = arc->audioPacketQueue.front();
					arc->audioPacketQueue.pop();
					avcodec_send_packet(arc->audioCodecContext, pkt);
					av_packet_free(&pkt);
				}else if(ret==AVERROR_EOF) {
					pushFinishEvent();
					av_frame_free(&frame);
					spdlog::debug("decode EOF");
					return;
				}else if(ret==0) {
					break;
				}else {
					spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
					av_frame_free(&frame);
				}
			}
			pos -= audioBuffer.size();
			arc->resampleAudioFrame(frame,audioBuffer);
		}
		bytesCopied = std::min(audioBuffer.size() - pos, static_cast<std::size_t>( len ));
		memcpy(stream, audioBuffer.data() + pos, bytesCopied);
		len -= bytesCopied;
		stream += bytesCopied;
		pos += bytesCopied;
		bytesPlayed += bytesCopied;
	}
}


bool ArcVP::setupAudioDevice(int sampleRate){
	SDL_AudioSpec targetSpec;
	// Set audio settings from codec info
	targetSpec.freq = sampleRate;
	targetSpec.format = AUDIO_F32;
	targetSpec.channels = audioCodecContext->ch_layout.nb_channels;
	targetSpec.silence = 0;
	targetSpec.samples = 4096;
	targetSpec.callback = audioCallback;
	targetSpec.userdata = this;
	audioDeviceID = SDL_OpenAudioDevice(audioDeviceName,
	                                    false,
	                                    &targetSpec,
	                                    &audioSpec,
	                                    false);
	if (audioDeviceID <= 0) {
		spdlog::error("SDL_OpenAudio: {}", SDL_GetError());
		return false;
	}
	SDL_PauseAudioDevice(audioDeviceID,false);

	return true;
}
