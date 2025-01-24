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

int64_t ptsToTime(int64_t pts, AVRational timebase){
	return pts * 1000. * timebase.num / timebase.den;
}

int64_t timeToPts(int64_t milli, AVRational timebase){
	return milli / 1000. * timebase.den / timebase.num;
}


void ArcVP::startPlayback(){
	SDL_GetDefaultAudioInfo(&audioDeviceName, &audioSpec, false);
	spdlog::info("default audio device: {}", audioDeviceName);
	if (!setupAudioDevice(audioCodecContext->sample_rate)) {
		spdlog::info("Unable to setup audio device: {}", audioDeviceName);
		return;
	}

	if (!decodeProducer) {
		decodeProducer = std::make_unique<std::thread>([this]{ this->decodeProduceThreadBody(); });
	}
	if (!decodeConsumer) {
		decodeConsumer = std::make_unique<std::thread>([this]{ this->decodeConsumeThreadBody(); });
	}
}

void ArcVP::decodeProduceThreadBody(){
	// demux all packets from the format context
	while (running.load()) {
		AVPacket* pkt = av_packet_alloc();
		if (!pkt) {
			spdlog::error("Failed to allocate AVPacket");
			break;
		} {
			std::unique_lock lk{fmtMtx};
			int ret = av_read_frame(formatContext, pkt);
			if (ret < 0) {
				av_packet_free(&pkt);
				if (ret == AVERROR_EOF) {
					break;
				}

				spdlog::error("Error reading frame: {}", av_err2str(ret));
				break;
			}
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
}


void ArcVP::decodeConsumeThreadBody(){
	videoStart = system_clock::now();
	while (this->running.load()) {
		AVFrame* frame = av_frame_alloc();
		std::unique_lock video{videoMtx};
		while (true) {
			int ret = avcodec_receive_frame(videoCodecContext, frame);;
			if (ret == 0) {
				break;
			}
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				auto pkt = videoPacketQueue.receive();
				if (!pkt) {
					return;
				}
				ret = avcodec_send_packet(videoCodecContext, pkt.value());

				if (ret < 0) {
					spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
				}
				av_packet_free(&pkt.value());
			}
			else {
				spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
				av_frame_free(&frame);
			}
		}
		int64_t pTimeMilli =ptsToTime(frame->pts,videoStream->time_base);

		while (pause.load() && running.load()) {
			videoStart = system_clock::now() - milliseconds(static_cast<int>( pTimeMilli / speed ));
			std::this_thread::sleep_for(10ms);
		}

		auto pTime = videoStart + milliseconds(static_cast<int>( pTimeMilli / speed ));
		prevFramePts=frame->pts;
		video.unlock();

		std::this_thread::sleep_until(pTime);
		pushNextFrameEvent(frame);
	}
}


bool ArcVP::resampleAudioFrame(AVFrame* frame){
	static SwrContext* ctx = nullptr;
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
	uint8_t** converted_data = nullptr;
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

	audioBuffer.resize(outSamples * audioCodecContext->ch_layout.nb_channels * sizeof(float));
	uint8_t* outBuffer = audioBuffer.data();
	swr_convert(ctx,
	            &outBuffer,
	            outSamples,
	            frame->data,
	            frame->nb_samples);
	return true;
}

void audioCallback(void* userdata, Uint8* stream, int len){
	auto arc = static_cast<ArcVP *>( userdata );
	static AVFrame* frame = av_frame_alloc();


	while (len > 0) {
		std::unique_lock lk{arc->audioMtx};
		auto &pos=arc->audioPos;
		int bytesCopied = 0;
		if (pos >= arc->audioBuffer.size()) {
			/* We have already sent all our data; get more */
			while (true) {
				int ret = avcodec_receive_frame(arc->audioCodecContext, frame);
				if (ret == 0) {
					break;
				}
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					auto pkt = arc->audioPacketQueue.receive();

					if (!pkt) {
						return;
					}

					avcodec_send_packet(arc->audioCodecContext, pkt.value());

					av_packet_free(&pkt.value());
				}
				else {
					spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
					av_frame_free(&frame);
				}
			}
			pos -= arc->audioBuffer.size();
			arc->resampleAudioFrame(frame);
			// spdlog::debug("frame pts: {}",frame->pts);

			// arc->audioSyncTo(frame);
		}
		// spdlog::debug("audio frame pts: {}",frame->pts);
		bytesCopied = std::min(arc->audioBuffer.size() - pos, static_cast<std::size_t>( len ));
		memcpy(stream, arc->audioBuffer.data() + pos, bytesCopied);
		len -= bytesCopied;
		stream += bytesCopied;
		pos += bytesCopied;
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
	SDL_PauseAudioDevice(audioDeviceID, false);

	return true;
}


constexpr int AUDIO_SYNC_THRESHOLD = 100;

void ArcVP::audioSyncTo(AVFrame* frame){
	auto pTime = videoStart + milliseconds(ptsToTime(frame->pts, audioStream->time_base));
	auto now = system_clock::now();
	auto nowms=duration_cast<milliseconds>(now.time_since_epoch()).count();
	auto pTimems=duration_cast<milliseconds>(pTime.time_since_epoch()).count();
	spdlog::debug("ptime - now = {}ms",pTimems-nowms);
	std::int64_t deltaTime = std::abs(duration_cast<milliseconds>(now - pTime).count());
	if (deltaTime < AUDIO_SYNC_THRESHOLD) {
		return;
	}
	if (now < pTime) {
		spdlog::debug("audio callback sleep: {}ms", deltaTime);
		std::this_thread::sleep_until(pTime);
	}
	else {
		auto sampleRate = audioCodecParams->sample_rate;
		int64_t samples = deltaTime * sampleRate / 1000.;
		audioBuffer.erase(
			std::remove_if(
				begin(audioBuffer),
				end(audioBuffer),
				[=, i = 0, cnt = 0](std::uint8_t)mutable{
					if (cnt >= samples)return false;
					if (i++ & 1) {
						cnt++;
						return true;
					}
					return false;
				}
			),
			end(audioBuffer)
		);
	}
}
