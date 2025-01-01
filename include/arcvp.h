//
// Created by delta on 12/30/2024.
//

#ifndef ARCVP_H
#define ARCVP_H

#include <memory>
#include <thread>
#include <utility>
#include <vector>
#include <queue>
#include <optional>
#include <condition_variable>
#include <semaphore>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class ArcVP {
  AVFormatContext *formatContext = nullptr;
  const AVCodec *videoCodec = nullptr;
  const AVCodec *audioCodec = nullptr;
  AVCodecContext *videoCodecContext = nullptr;
  AVCodecContext *audioCodecContext = nullptr;
  AVCodecParameters *videoCodecParams = nullptr;
  AVCodecParameters *audioCodecParams = nullptr;

  AVStream*videoStream = nullptr;
  AVStream*audioStream = nullptr;
  AVStream*subtitleStream = nullptr;

  std::unique_ptr<std::thread> decoderThread = nullptr, playerThread = nullptr;
  std::queue<AVPacket*> videoPacketQueue{},audioPacketQueue{};

  std::mutex lkPlaybackEvent{};
  std::condition_variable cvEvent;
  std::queue<int> playbackEvents{};
  AVFrame* curVideoFrame=nullptr;

  ArcVP() {
    curVideoFrame=av_frame_alloc();
  }

  void decoderFunc();
  void playerFunc();
  void setupAudioDevice();

 bool tryReceiveVideoFrame();
  bool tryReceiveAudioFrame();

public:

  ArcVP(const ArcVP &) = delete;
  ArcVP &operator=(const ArcVP &) = delete;

  static bool start() {
    instance().decoderThread = std::make_unique<std::thread>([] {
      instance().decoderFunc();
    });
    instance().playerThread = std::make_unique<std::thread>([] {
      instance().playerFunc();
    });
    return true;
  }
  static bool destroy() {
    instance().decoderThread->join();
    instance().decoderThread = nullptr;
    instance().playerThread->join();
    instance().playerThread = nullptr;
    return true;
  }

  static ArcVP &instance() {
    static ArcVP instance;
    return instance;
  }

  bool open(const char *path);
  bool close();

  bool resume();
  bool pause();

  bool setPlaybackSpeed();

  bool seek(uint64_t tick);

  void sendPresentVideoEvent() {
    std::unique_lock lock{lkPlaybackEvent};
    playbackEvents.push(0);
    cvEvent.notify_one();
  }
  void sendPresentAudioEvent() {
    std::unique_lock lock{lkPlaybackEvent};
    playbackEvents.push(1);
    cvEvent.notify_one();
  }

  uint64_t nextVideoFramePresentTimeMs = 0;
  bool getNextFrame();
  [[nodiscard]] const AVFrame* getCurFrame()const {
    return curVideoFrame;
  }

};

#endif // ARCVP_H
