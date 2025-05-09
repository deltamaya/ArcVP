//
// Created by delta on 1/22/2025.
//

#include "player.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace ArcVP {
void pushFinishEvent() {
  SDL_Event event;
  event.type = ARCVP_FINISH_EVENT;
  SDL_PushEvent(&event);
}

void Player::packetDecodeThreadWorker() {
  // demux all packets from the format context
  while (true) {
    std::unique_lock status_lock{packet_decode_worker_.mtx};
    packet_decode_worker_.cv.wait(status_lock, [this] {
      return packet_decode_worker_.status != WorkerStatus::Idle;
    });
    if (packet_decode_worker_.status == WorkerStatus::Exiting) {
      break;
    }
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
      spdlog::error("Fail to allocate AVPacket");
      std::exit(1);
    }
    int ret;
    {
      std::scoped_lock lk{media_context_.format_mtx_};
      ret = av_read_frame(media_context_.format_context_, pkt);
    }
    if (ret < 0) {
      av_packet_free(&pkt);
      if (ret == AVERROR_EOF) {
        packet_decode_worker_.status = WorkerStatus::Idle;
        spdlog::info("Packet decode worker idled due to EOF");
        continue;
      }
      spdlog::error("Error reading frame: {}", av_err2str(ret));
      std::exit(1);
    }
    if (pkt->stream_index == media_context_.video_stream_index_) {
      bool ok = video_packet_channel_.send(pkt);
      if (!ok) {
        packet_decode_worker_.status = WorkerStatus::Exiting;
      }
    } else if (pkt->stream_index == media_context_.audio_stream_index_) {
      bool ok = audio_packet_channel_.send(pkt);
      if (!ok) {
        packet_decode_worker_.status = WorkerStatus::Exiting;
      }
    } else {
      spdlog::warn("Unknown packet index: {}", pkt->stream_index);
      av_packet_free(&pkt);
    }
  }
  spdlog::info("Packet decode thread exited");
}
}  // namespace ArcVP