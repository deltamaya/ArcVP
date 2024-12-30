//
// Created by delta on 12/30/2024.
//

#ifndef ARCVP_H
#define ARCVP_H

#include <memory>
#include <thread>
#include <vector>
#include <queue>

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
  std::queue<AVPacket*> videoPacketQueue,audioPacketQueue;

  std::vector<uint8_t> videoFrameBuffer, audioFrameBuffer;

  ArcVP() = default;

  void decoderFunc();
  void playerFunc();
  void setupAudioDevice();

public:
  ArcVP(const ArcVP &) = delete;
  ArcVP &operator=(const ArcVP &) = delete;

  static bool init() {
    getInstance().decoderThread = std::make_unique<std::thread>(decoderFunc);
    getInstance().playerThread = std::make_unique<std::thread>(playerFunc);
    return true;
  }
  static bool destroy() {
    getInstance().decoderThread->join();
    getInstance().decoderThread = nullptr;
    getInstance().playerThread->join();
    getInstance().playerThread = nullptr;
    return true;
  }

  static ArcVP &getInstance() {
    static ArcVP instance;
    return instance;
  }

  bool open(const char *path);
  bool close();

  bool resume();
  bool pause();

  bool setPlaybackSpeed();

  bool seek(uint64_t tick);
};

#endif // ARCVP_H
