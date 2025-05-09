//
// Created by delta on 5/9/2025.
//

#ifndef DECODE_WORKER_H
#define DECODE_WORKER_H
#include <memory>
#include <thread>
#include "player.h"
enum class WorkerStatus { Working, Idle, Exiting };
namespace ArcVP {
  struct DecodeWorker {
    std::unique_ptr<std::thread> th=nullptr;
    std::mutex mtx{};
    std::condition_variable cv;
    WorkerStatus status=WorkerStatus::Idle;

    explicit DecodeWorker() {

    }

    template<typename Fn,typename... Args>
    void spawn(Fn&& func,Args&& ...args) {
      status=WorkerStatus::Working;
      th=std::make_unique<std::thread>(std::forward<Fn>(func),std::forward<Args>(args)...);
    }

    void join() {
      if (th)
        th->join();
      status=WorkerStatus::Idle;
    }
  };
}
#endif //DECODE_WORKER_H
