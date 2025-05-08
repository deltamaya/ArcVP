//
// Created by delta on 5/8/2025.
//
#include "arcvp.h"

void ArcVP::playbackWorker(){
	while (running) {
		while (playbackWorkerStatus == WorkerStatus::Idle) {
			std::this_thread::sleep_for(10ms);
		}
		if (playbackWorkerStatus == WorkerStatus::Exiting) {
			break;
		}
		AVFrame* frame = av_frame_alloc();
		int ret = 0;
		while (true) {
			std::scoped_lock video{videoMtx};
			ret = avcodec_receive_frame(videoCodecContext, frame);
			if (ret == 0) {
				break;
			}
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
				auto pkt = videoPacketQueue.receive();
				if (!pkt) {
					spdlog::info("Video Consumer exited due to closed channel");
					goto end;
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

		int64_t presentTimeMs = ptsToTime(frame->pts, videoStream->time_base);
		int64_t elapsedMs = duration_cast<milliseconds>(system_clock::now() - videoStart).count();
		{
			std::scoped_lock renderQueueLk{renderQueueMtx};
			if (presentTimeMs < elapsedMs || !renderQueue.empty() && renderQueue.back().presentTimeMs >=
			    presentTimeMs) {
				spdlog::info("Video: Dropped frame at {}s", presentTimeMs / 1000.);
				av_frame_free(&frame);
				continue;
			}
		}
		renderQueueEmpty.acquire();
		{
			std::scoped_lock renderQueueLk{renderQueueMtx};
			renderQueue.emplace_back(frame, presentTimeMs);
			spdlog::debug("video frame push back: {}s",presentTimeMs/1000.);
		}
		renderQueueReady.release();
	}
end:
	spdlog::info("Playback Thread Exited");
}
