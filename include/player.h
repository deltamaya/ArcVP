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
struct DisposeAVPacket {
  void operator()(AVPacket* pkt) const { av_packet_free(&pkt); }
};

int64_t ptsToTime(int64_t pts, AVRational timebase);

int64_t timeToPts(int64_t milli, AVRational timebase);
enum ArcVPEvent {
  ARCVP_NEXTFRAME_EVENT = SDL_USEREVENT + 1,
  ARCVP_FINISH_EVENT,
};

class Player {
  enum class WorkerStatus { Working, Idle, Exiting };

  SyncState sync_state_;
  MediaContext media_context_;

  std::unique_ptr<std::thread> packet_decode_thread_{}, video_decode_thread_{},
      audio_decode_thread_{};
  std::atomic<WorkerStatus> packet_decode_worker_status_ = WorkerStatus::Idle,
                            video_decode_worker_status_ = WorkerStatus::Idle,
                            audio_decode_worker_status_ = WorkerStatus::Idle;
  Channel<AVPacket*, 256, DisposeAVPacket> video_packet_channel_,
      audio_packet_channel_;

  struct RenderEntry {
    AVFrame* frame = nullptr;
    int64_t present_ms = -1;
  };
  struct DisposeRenderEntry {
    void operator()(RenderEntry entry) const { av_frame_free(&entry.frame); }
  };

  FrameQueue audio_frame_queue_{100},video_frame_queue_{200};

  AudioDevice audio_device_{};

  std::vector<uint8_t> audio_buffer_{};

  int width = -1, height = -1;

  int64_t audioPos = 0, prevFramePts = AV_NOPTS_VALUE;
  double speed = 1.;

  void packetDecodeThreadWorker();

  void videoDecodeThreadWorker();

  void audioDecodeThreadWorker();

  bool setupAudioDevice(int);
  Player() = default;
  inline static Player* instance_ptr=nullptr;
 public:
  static Player* instance() {
    if (!instance_ptr) {
      instance_ptr=new Player();
    }
    return instance_ptr;
  }
  AVFrame* tryFetchAudioFrame() {
    int64_t played_ms=getPlayedMs();
    std::scoped_lock lk{audio_frame_queue_.mtx};
    if (audio_frame_queue_.queue.empty()) {
      return nullptr;
    }
    auto front=audio_frame_queue_.queue.front();
    if (front.present_ms>=played_ms) {
      audio_frame_queue_.semReady.acquire();
      audio_frame_queue_.queue.pop_front();
      audio_frame_queue_.semEmpty.release();
      return front.frame;
    }
    return nullptr;

  }

  AVFrame* tryFetchVideoFrame() {
    int64_t played_ms=getPlayedMs();
    std::scoped_lock lk{video_frame_queue_.mtx};
    if (video_frame_queue_.queue.empty()) {
      return nullptr;
    }
    auto front=video_frame_queue_.queue.front();
    if (front.present_ms>=played_ms) {
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

    packet_decode_worker_status_ = WorkerStatus::Exiting;
    video_decode_worker_status_ = WorkerStatus::Exiting;
    audio_decode_worker_status_ = WorkerStatus::Exiting;

    video_packet_channel_.close();
    audio_packet_channel_.close();

    {
      std::scoped_lock lk{video_frame_queue_.mtx};
      while (!video_frame_queue_.queue.empty()) {
        av_frame_free(&video_frame_queue_.queue.front().frame);
        video_frame_queue_.queue.pop_front();
      }
    }


    {
      std::scoped_lock lk{audio_frame_queue_.mtx};
      while (!audio_frame_queue_.queue.empty()) {
        av_frame_free(&audio_frame_queue_.queue.front().frame);
        audio_frame_queue_.queue.pop_front();
      }
    }

    if (packet_decode_thread_) {
      packet_decode_thread_->join();
    }
    if (video_decode_thread_) {
      video_decode_thread_->join();
    }
    if (audio_decode_thread_) {
      audio_decode_thread_->join();
    }
  }

  bool open(const char*);

  void close();

  void startPlayback();

  void togglePause();

  void seekTo(std::int64_t milli);

  void speedUp();

  void speedDown();

  void audioSyncTo(AVFrame* frame);

  std::int64_t getAudioPlayTime(std::int64_t bytesPlayed);

  std::tuple<int, int> getWH() { return std::make_tuple(width, height); }

  int64_t getPlayedMs() {
    std::scoped_lock lk{sync_state_.mtx_, media_context_.audio_codec_mtx_};
    return sync_state_.sample_count_ /
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
