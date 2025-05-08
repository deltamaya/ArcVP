//
// Created by delta on 5/8/2025.
//

#ifndef FRAME_QUEUE_H
#define FRAME_QUEUE_H
extern "C"{
#include <libavutil/frame.h>
}

#include "player.h"
namespace ArcVP {
  struct FrameQueue {
    struct RenderEntry {
      AVFrame* frame;
      int64_t present_ms;
    };
    std::deque<RenderEntry> queue;
    std::mutex mtx;
    std::counting_semaphore<> semReady, semEmpty;

    explicit FrameQueue(int size):semReady(0),semEmpty(size) {

    }
  };
}

#endif //FRAME_QUEUE_H
