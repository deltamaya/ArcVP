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
#include "imgui.h"
#include "backends/imgui_impl_sdl3.h"
#include "backends/imgui_impl_sdlrenderer3.h"
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

struct NextFrameEvent {
  AVFrame* frame;
  int64_t present_ms;
};

class Player {
  MediaContext media_{};


  DecodeWorker audio_decode_worker_, video_decode_worker_;

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
  void demuxAllPackets();

  AVFrame* decodeVideoFrame();
  AVFrame* decodeAudioFrame();

 public:
  void controlPanel();
  AVFrame* getVideoFrame() {
    if (video_decode_worker_.output_queue.queue.empty()) {
      return nullptr;
    }
    auto front=video_decode_worker_.output_queue.queue.front();
    if (front.present_ms==AV_NOPTS_VALUE) {
      sync_state_.should_exit=true;
      return nullptr;
    }
    int64_t played_ms=getPlayedMs();
    if (played_ms>=front.present_ms) {
      // display this frame
      video_decode_worker_.output_queue.semReady.acquire();
      video_decode_worker_.output_queue.mtx.lock();
      video_decode_worker_.output_queue.queue.pop_front();
      video_decode_worker_.output_queue.mtx.unlock();
      video_decode_worker_.output_queue.semEmpty.release();
      return front.frame;
    }
    return nullptr;
  }

  static Player* instance() {
    if (!instance_ptr) {
      instance_ptr = new Player();
    }
    return instance_ptr;
  }


  bool resampleAudioFrame(AVFrame* frame);

  ~Player() {
    sync_state_.should_exit=true;
    video_decode_worker_.output_queue.mtx.lock();
    while (!video_decode_worker_.output_queue.queue.empty()) {
      auto frame=video_decode_worker_.output_queue.queue.front().frame;
      av_frame_free(&frame);
      video_decode_worker_.output_queue.queue.pop_front();
      video_decode_worker_.output_queue.semEmpty.release();
    }
    video_decode_worker_.output_queue.mtx.unlock();



    video_decode_worker_.join();
    audio_decode_worker_.join();
  }


  bool open(const char*);

  void close();

  void exit() {
    // close();
    delete instance_ptr;
    instance_ptr = nullptr;
  }

  void startPlayback();
  friend void audioCallback(void* userdata, SDL_AudioStream* stream, int ,int);

  void togglePause() {
    if (sync_state_.pause) {
      unpause();
    }else {
      pause();
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
  SyncState sync_state_{};

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
