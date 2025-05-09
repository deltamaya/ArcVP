//
// Created by delta on 1/23/2025.
//

#ifndef CHANNEL_H
#define CHANNEL_H

#include <deque>
#include <mutex>
#include <optional>
#include <semaphore>
namespace ArcVP {

template <typename T, size_t MaxSize, typename DelFunc = void>
class Channel {
  ::std::deque<T> queue;
  std::mutex mtx;
  std::counting_semaphore<> semEmpty = std::counting_semaphore(MaxSize);
  std::counting_semaphore<> semReady = std::counting_semaphore(0);
  std::atomic_bool closed = false;

 public:
  bool send(const T& value) {
    if (closed) {
      spdlog::warn("Sending to a closed channel");
      return false;
    }
    semEmpty.acquire();
    std::scoped_lock lk{mtx};
    if (closed) {
      spdlog::warn("Sending to a closed channel");
      return false;
    }
    queue.push_back(value);
    semReady.release();
    return true;
  }

  int64_t size() {
    std::scoped_lock lk{mtx};
    return queue.size();
  }

  bool empty() {
    std::scoped_lock lk{mtx};
    return queue.empty();
  }

  std::optional<T> receive() {
    if (closed) {
      spdlog::warn("Receiving from a closed channel");
      return std::nullopt;
    }
    semReady.acquire();
    std::scoped_lock lk{mtx};
    if (closed) {
      spdlog::warn("Receiving from a closed channel");
      return std::nullopt;
    }
    // asserts the queue is not empty
    assert(queue.size() != 0);
    std::optional<T> ret(std::move(queue.front()));
    queue.pop_front();
    semEmpty.release();
    return ret;
  }

  std::optional<T> front() {
    std::scoped_lock lk{mtx};
    if (closed) {
      spdlog::warn("Peeking a closed channel");
      return std::nullopt;
    }
    return queue.front();
  }

  std::optional<T> back() {
    std::scoped_lock lk{mtx};
    if (closed) {
      spdlog::warn("Peeking a closed channel");
      return std::nullopt;
    }
    return queue.back();
  }

  void clear() {
    std::scoped_lock lk{mtx};
    while (!queue.empty()) {
      semReady.acquire();
      auto item = queue.front();
      DelFunc{}(item);
      queue.pop_front();
      semEmpty.release();
    }
  }
  void close() {
    if (closed) {
      spdlog::warn("Closing a closed channel");
    }
    closed = true;
    clear();
  }

  bool is_closed() const { return closed; }
};
}  // namespace ArcVP

#endif  // CHANNEL_H
