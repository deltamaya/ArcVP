//
// Created by delta on 5/8/2025.
//
#include "arcvp.h"
void ArcVP::audioDecodeWorker(){
	while (running) {
		while (audioDecoderWorkerStatus==WorkerStatus::Idle) {
			std::this_thread::sleep_for(10ms);
		}
		if (audioDecoderWorkerStatus==WorkerStatus::Exiting) {
			break;
		}
		int64_t elapsedMs = duration_cast<milliseconds>(system_clock::now() - videoStart).count();
		AVFrame* frame=av_frame_alloc();
		int ret = 0;
		while (true) {
			std::scoped_lock audio{audioMtx};
			ret = avcodec_receive_frame(audioCodecContext, frame);
			if (ret == 0) {
				break;
			}
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				// spdlog::debug("audio thread receive packet with lock");
				auto pkt = audioPacketQueue.receive();
				// spdlog::debug("packet acquired");
				if (!pkt) {
					spdlog::info("Audio Consumer exited due to closed channel");
					goto end;
				}
				ret = avcodec_send_packet(audioCodecContext, pkt.value());
				if (ret < 0) {
					spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
				}
				av_packet_free(&pkt.value());
			}
			else {
				spdlog::error("Unable to receive audio frame: {}", av_err2str(ret));
				av_frame_free(&frame);
			}
		}


		// spdlog::debug("audio try lock audio queue");
		int64_t presentTimeMs = ptsToTime(frame->pts, audioStream->time_base);

		{
			std::scoped_lock renderQueueLk{renderQueueMtx};
			// spdlog::debug("lock done");
			if (presentTimeMs < elapsedMs || !audioQueue.empty() && audioQueue.back().presentTimeMs >=
				presentTimeMs) {
				spdlog::info("Video: Dropped frame at {}s", presentTimeMs / 1000.);
				av_frame_free(&frame);
				continue;
				}
		}

		// spdlog::debug("audio try acquire empty semaphore");
		audioQueueEmpty.acquire();
		// spdlog::debug("semaphore acquired");
		{
			std::scoped_lock audioQueueLk{audioQueueMtx};
			audioQueue.emplace_back(frame, presentTimeMs);
			spdlog::debug("audio push back: {}s, videoStart: {}s",presentTimeMs/1000.,videoStart.time_since_epoch().count()/1e9);
		}
		audioQueueReady.release();

	}
	end:
	spdlog::info("Audio Decoder Thread Exited");
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


	while (len > 0) {
		auto&pos = arc->audioPos;
		int bytesCopied = 0;
		if (pos >= arc->audioBuffer.size()) {
			AVFrame* frame=arc->tryFetchAudioFrame();
			if (!frame) {
				spdlog::info("AudioCallback: no available frame");
				return;
			}

			pos = 0;
			arc->resampleAudioFrame(frame);

			arc->audioSyncTo(frame);
		}
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

	return true;
}


constexpr int AUDIO_SYNC_THRESHOLD = 100;

void ArcVP::audioSyncTo(AVFrame* frame){
	int64_t elapsedMs = duration_cast<milliseconds>(system_clock::now() - videoStart).count();
	int64_t presentTimeMs = ptsToTime(frame->pts, audioStream->time_base);
	std::int64_t deltaTime = presentTimeMs - elapsedMs;
	// spdlog::debug("Audio: deltaTime: {}ms",deltaTime);
	if (std::abs(deltaTime) < AUDIO_SYNC_THRESHOLD) {
		return;
	}
	if (deltaTime > 0) {
		int64_t samples = deltaTime * audioCodecParams->sample_rate / 1000.;
		while (samples--) {
			audioBuffer.push_back(0);
		}
		spdlog::info("Audio: Sync to: {}ms", deltaTime);
	}
	else {
		auto sampleRate = audioCodecParams->sample_rate;
		int64_t samples = std::abs(deltaTime) * sampleRate / 1000.;
		auto end = samples > audioBuffer.size() ? audioBuffer.end() : audioBuffer.begin() + samples;
		audioBuffer.erase(audioBuffer.begin(), end);
		spdlog::info("Audio: Sync to: {}ms", deltaTime);
	}
}
