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
#include <SDL3/SDL.h>
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
  ARCVP_EVENT_NEXTFRAME = SDL_EVENT_USER + 1,
  ARCVP_EVENT_FINISH,
};

class Player {
  SyncState sync_state_{};
  MediaContext media_{};

  DecodeWorker audio_decode_worker_, video_decode_worker_,
      packet_decode_worker_;

  AudioDevice audio_device_{};

  std::vector<uint8_t> audio_buffer_{};
  SDL_AudioStream* audio_stream=nullptr;

  int width = -1, height = -1;

  double speed = 1.;

  void packetDecodeThreadWorker();

  void videoDecodeThreadWorker();

  void audioDecodeThreadWorker();

  bool setupAudioDevice();
  Player() {};
  inline static Player* instance_ptr = nullptr;

 public:
  static Player* instance() {
    if (!instance_ptr) {
      instance_ptr = new Player();
    }
    return instance_ptr;
  }


  bool resampleAudioFrame(AVFrame* frame);

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
  friend void audioCallback(void* userdata, SDL_AudioStream* stream, int ,int);

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
           media_.audio_codec_params_->sample_rate;
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
