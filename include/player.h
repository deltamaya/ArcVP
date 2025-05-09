//
// Created by delta on 1/22/2025.
//

#ifndef ARCVP_H
#define ARCVP_H

#include <spdlog/spdlog.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "audio_device.h"
#include "channel.h"
#include "decode_worker.h"
#include "frame_queue.h"
#include "media_context.h"
#include "sync_state.h"

extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace ArcVP {

int64_t ptsToTime(int64_t pts, AVRational timebase);

int64_t timeToPts(int64_t milli, AVRational timebase);
enum ArcVPEvent {
  ARCVP_NEXTFRAME_EVENT = SDL_USEREVENT + 1,
  ARCVP_FINISH_EVENT,
};

class Player {
  SyncState sync_state_;
  MediaContext media_context_;

  DecodeWorker audio_decode_worker_, video_decode_worker_,
      packet_decode_worker_;

  struct DisposeAVPacket {
    void operator()(AVPacket* pkt) const { av_packet_free(&pkt); }
  };
  Channel<AVPacket*, 256, DisposeAVPacket> video_packet_channel_,
      audio_packet_channel_;
  FrameQueue audio_frame_queue_{100}, video_frame_queue_{200};

  AudioDevice audio_device_{};

  std::vector<uint8_t> audio_buffer_{};

  int width = -1, height = -1;

  double speed = 1.;

  void packetDecodeThreadWorker();

  void videoDecodeThreadWorker();

  void audioDecodeThreadWorker();

  bool setupAudioDevice(int);
  Player() = default;
  inline static Player* instance_ptr = nullptr;

 public:
  static Player* instance() {
    if (!instance_ptr) {
      instance_ptr = new Player();
    }
    return instance_ptr;
  }
  AVFrame* tryFetchAudioFrame() {
    std::scoped_lock lk{audio_frame_queue_.mtx, sync_state_.mtx_};
    int64_t played_ms = getPlayedMs();
    if (audio_frame_queue_.queue.empty()) {
      return nullptr;
    }
    do {
      auto front = audio_frame_queue_.queue.front();
      audio_frame_queue_.semReady.acquire();
      audio_frame_queue_.queue.pop_front();
      audio_frame_queue_.semEmpty.release();
      if (front.present_ms >= played_ms) {
        return front.frame;
      }
    } while (!audio_frame_queue_.queue.empty());

    return nullptr;
  }

  AVFrame* tryFetchVideoFrame() {
    std::scoped_lock lk{video_frame_queue_.mtx, sync_state_.mtx_};
    if (sync_state_.status_ != InstanceStatus::Playing) {
      return nullptr;
    }
    int64_t played_ms = getPlayedMs();
    if (video_frame_queue_.queue.empty()) {
      return nullptr;
    }
    auto front = video_frame_queue_.queue.front();
    if (front.present_ms <= played_ms) {
      video_frame_queue_.semReady.acquire();
      video_frame_queue_.queue.pop_front();
      video_frame_queue_.semEmpty.release();
      return front.frame;
    }
    return nullptr;
  }

  bool resampleAudioFrame(AVFrame* frame);

  friend void audioCallback(void* userdata, Uint8* stream, int len);

  ~Player() {
    sync_state_.status_ = InstanceStatus::Exiting;

    {
      std::scoped_lock status_lock{packet_decode_worker_.mtx};
      packet_decode_worker_.status = WorkerStatus::Exiting;
      packet_decode_worker_.cv.notify_all();
    }
    {
      std::scoped_lock status_lock{audio_decode_worker_.mtx};
      audio_decode_worker_.status = WorkerStatus::Exiting;
      audio_decode_worker_.cv.notify_all();
    }
    {
      std::scoped_lock status_lock{video_decode_worker_.mtx};
      video_decode_worker_.status = WorkerStatus::Exiting;
      video_decode_worker_.cv.notify_all();
    }

    video_frame_queue_.clear();
    audio_frame_queue_.clear();

    video_packet_channel_.close();
    audio_packet_channel_.close();

    packet_decode_worker_.join();
    audio_decode_worker_.join();
    video_decode_worker_.join();
  }

  bool open(const char*);

  void close();

  void exit() {
    close();
    delete instance_ptr;
    instance_ptr = nullptr;
  }

  void startPlayback();

  void togglePause() {
    std::scoped_lock lk{sync_state_.mtx_};
    if (sync_state_.status_==InstanceStatus::Pause) {
      unpause();
    }
    else if (sync_state_.status_==InstanceStatus::Playing){
      pause();
    }else {
      spdlog::warn("Toggle pause state in invalid status");
      std::exit(1);
    }
  }
  void pause();
  void unpause();

  void seekTo(std::int64_t milli);

  void speedUp();

  void speedDown();

  void audioSyncTo(AVFrame* frame, int64_t played_ms);

  std::int64_t getAudioPlayTime(std::int64_t bytesPlayed);

  std::tuple<int, int> getWH() { return std::make_tuple(width, height); }

  int64_t getPlayedMs() {
    return sync_state_.sample_count_ * 1000. /
           media_context_.audio_codec_params_->sample_rate;
  }
};

using namespace std::chrono;

inline int64_t ptsToTime(int64_t pts, AVRational timebase) {
  return pts * 1000. * timebase.num / timebase.den;
}

inline int64_t timeToPts(int64_t milli, AVRational timebase) {
  return milli / 1000. * timebase.den / timebase.num;
}
}  // namespace ArcVP
#endif  // ARCVP_H
