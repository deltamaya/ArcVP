//
// Created by delta on 5/8/2025.
//

#ifndef SYNC_STATE_H
#define SYNC_STATE_H
#include <chrono>
#include <cstdint>
#include <mutex>

namespace ArcVP {
using namespace std::chrono;
using namespace std::chrono_literals;
enum class InstanceStatus { Idle, Playing, Seeking, Pause, Exiting };
struct SyncState {
  steady_clock::time_point audio_start_{};
  int64_t sample_count_;
  InstanceStatus status_=InstanceStatus::Idle;
  std::mutex mtx_;
};
}  // namespace ArcVP

#endif  // SYNC_STATE_H
