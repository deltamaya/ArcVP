//
// Created by delta on 5/8/2025.
//
#include "player.h"
namespace ArcVP {
void Player::videoDecodeThreadWorker() {
  while (true) {
    std::unique_lock status_lock{video_decode_worker_.mtx};
    video_decode_worker_.cv.wait(status_lock, [this] {
      return video_decode_worker_.status != WorkerStatus::Idle;
    });
    if (video_decode_worker_.status == WorkerStatus::Exiting) {
      break;
    }
    AVFrame* frame = av_frame_alloc();
    int ret = 0;
    while (true) {
      std::scoped_lock video{media_context_.video_codec_mtx_};
      ret = avcodec_receive_frame(media_context_.video_codec_context_, frame);
      if (ret == 0) {
        break;
      }
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        auto pkt = video_packet_channel_.receive();
        if (!pkt) {
          spdlog::info("Video Consumer exited due to closed channel");
          video_decode_worker_.status = WorkerStatus::Exiting;
          goto end;
        }
        ret = avcodec_send_packet(media_context_.video_codec_context_,
                                  pkt.value());
        if (ret < 0) {
          spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
        }
        av_packet_free(&pkt.value());
      } else {
        spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
        av_frame_free(&frame);
      }
    }
    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms =
        ptsToTime(frame->pts, media_context_.video_stream_->time_base);

    {
      std::scoped_lock lk{video_frame_queue_.mtx, sync_state_.mtx_};
      int64_t played_ms = getPlayedMs();
      // spdlog::debug("lock done");
      if (present_ms < played_ms ||
          !video_frame_queue_.queue.empty() &&
              video_frame_queue_.queue.back().present_ms >= present_ms) {
        spdlog::info("Video: Dropped frame at {}s", present_ms / 1000.);
        av_frame_free(&frame);
        continue;
      }
    }

    // spdlog::debug("audio try acquire empty semaphore");
    // spdlog::debug("semaphore acquired");

    video_frame_queue_.semEmpty.acquire();
    video_frame_queue_.queue.emplace_back(frame, present_ms);
    video_frame_queue_.semReady.release();
  }
end:
  spdlog::info("Video decode thread exited");
}
}  // namespace ArcVP