//
// Created by delta on 12/30/2024.
//

#ifndef ARCVP_H
#define ARCVP_H

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class ArcVP {
  AVFormatContext *formatContext = nullptr;
  AVCodec *videoCodec = nullptr;
  AVCodec *audioCodec = nullptr;
  AVCodecContext *videoCodecContext = nullptr;
  AVCodecContext *audioCodecContext = nullptr;
  AVCodecParameters *videoCodecParams = nullptr;
  AVCodecParameters *audioCodecParams = nullptr;

  ArcVP() = default;

public:
  ArcVP(const ArcVP &) = delete;
  ArcVP &operator=(const ArcVP &) = delete;

  static ArcVP &getInstance() {
    static ArcVP instance;
    return instance;
  }
};

#endif // ARCVP_H
