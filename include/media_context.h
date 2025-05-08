//
// Created by delta on 5/8/2025.
//

#ifndef MEDIA_CONTEXT_H
#define MEDIA_CONTEXT_H

namespace ArcVP {
extern "C" {
#include <SDL2/SDL.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

struct MediaContext {
  AVFormatContext* format_context_ = nullptr;
  const AVCodec* video_codec_ = nullptr;
  const AVCodec* audio_codec_ = nullptr;
  AVCodecContext* video_codec_context_ = nullptr;
  AVCodecContext* audio_codec_context_ = nullptr;
  const AVCodecParameters* video_codec_params_ = nullptr;
  const AVCodecParameters* audio_codec_params_ = nullptr;
  const AVStream *video_stream_ = nullptr, *audio_stream_ = nullptr;
  int video_stream_index_ = -1, audio_stream_index_ = -1;
  std::mutex format_mtx_, video_codec_mtx_, audio_codec_mtx_;
};
}  // namespace ArcVP
#endif  // MEDIA_CONTEXT_H
