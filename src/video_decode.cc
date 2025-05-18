//
// Created by delta on 5/8/2025.
//
#include "player.h"
namespace ArcVP {

AVFrame* Player::decodeVideoFrame() {
  AVFrame* frame = av_frame_alloc();
  int ret = 0;
  while (true) {
    ret = avcodec_receive_frame(media_.video_codec_context_, frame);
    if (ret == 0) {
      break;
    }
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      if (video_decode_worker_.packet_chan.empty()) {
        av_frame_free(&frame);
        return nullptr;
      }
      auto pkt = video_decode_worker_.packet_chan.front();
      video_decode_worker_.packet_chan.pop_front();
      if (!pkt) {
        spdlog::info("Video Consumer exited due to closed channel");
        video_decode_worker_.status = WorkerStatus::Exiting;
        av_frame_free(&frame);
        return nullptr;
      }
      ret = avcodec_send_packet(media_.video_codec_context_, pkt);
      if (ret < 0) {
        spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
      }
      av_packet_free(&pkt);
    } else {
      spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
      av_frame_free(&frame);
    }
  }
  return frame;

}
void Player::videoDecodeThreadWorker() {
  while (!sync_state_.should_exit) {
    std::unique_lock lk{video_decode_worker_.mtx};
    AVFrame* frame=decodeVideoFrame();
    if (!frame) {
      video_decode_worker_.output_queue.queue.emplace_back(nullptr,AV_NOPTS_VALUE);
      break;
    }
    lk.unlock();
    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms = ptsToTime(frame->pts, media_.video_stream_->time_base);
    video_decode_worker_.output_queue.semEmpty.acquire();
    video_decode_worker_.output_queue.mtx.lock();
    video_decode_worker_.output_queue.queue.emplace_back(frame,present_ms);
    video_decode_worker_.output_queue.mtx.unlock();
    video_decode_worker_.output_queue.semReady.release();
  }
end:
  spdlog::info("Video decode thread exited");
}
}  // namespace ArcVP