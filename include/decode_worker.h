//
// Created by delta on 5/9/2025.
//

#ifndef DECODE_WORKER_H
#define DECODE_WORKER_H
extern "C"{
#include <libavcodec/packet.h>
}

#include <memory>
#include <thread>

#include "frame_queue.h"
#include "player.h"
enum class WorkerStatus { Working, Idle, Exiting };
namespace ArcVP {
struct DecodeWorker {
  std::unique_ptr<std::thread> th = nullptr;
  std::mutex mtx{};
  std::condition_variable cv;
  FrameQueue output_queue;
  struct DisposeAVPacket {
    void operator()(AVPacket* pkt) const { av_packet_free(&pkt); }
  };
  Channel<AVPacket*,200,DisposeAVPacket> packet_chan{};
  WorkerStatus status = WorkerStatus::Idle;

  explicit DecodeWorker() :output_queue(100){}

  template <typename Fn, typename... Args>
  void spawn(Fn&& func, Args&&... args) {
    status = WorkerStatus::Working;
    th = std::make_unique<std::thread>(std::forward<Fn>(func),
                                       std::forward<Args>(args)...);
  }

  void join() const {
    if (th&&th->joinable()) th->join();
  }
};
}  // namespace ArcVP
#endif  // DECODE_WORKER_H
