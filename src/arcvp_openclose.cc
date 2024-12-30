//
// Created by delta on 12/30/2024.
//

#include "arcvp.h"
#include <iostream>

using std::cout;

bool ArcVP::open(const char *path) {

  AVFormatContext *context = avformat_alloc_context();
  int ret = avformat_open_input(&context, path, nullptr, nullptr);
  if (ret != 0) {
    cout << "Unable to open input: " << av_err2str(ret) << "\n";
    return false;
  }
  this->formatContext = context;

  ret = avformat_find_stream_info(formatContext, nullptr);
  if (ret != 0) {
    cout << "Unable to find stream info: " << av_err2str(ret) << "\n";
    return false;
  }

  for (int i = 0; i < context->nb_streams; i++) {
    auto *stream = context->streams[i];
    const auto *params = stream->codecpar;
    if (!videoStream && params->codec_type == AVMEDIA_TYPE_VIDEO) {
      this->videoStream = stream;
    } else if (!audioStream && params->codec_type == AVMEDIA_TYPE_AUDIO) {
      this->audioStream = stream;
    } else if (!subtitleStream && params->codec_type == AVMEDIA_TYPE_SUBTITLE) {
      this->subtitleStream = stream;
    }
  }

  if (videoStream) {
    videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    videoCodecContext = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecContext, videoStream->codecpar);
    avcodec_open2(videoCodecContext, videoCodec, nullptr);
  }
  if (audioStream) {
    audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);
    audioCodecContext = avcodec_alloc_context3(audioCodec);
    avcodec_parameters_to_context(audioCodecContext, audioStream->codecpar);
    avcodec_open2(audioCodecContext, audioCodec, nullptr);
  }

  return true;
}

bool ArcVP::close() {
  if (formatContext) {
    avformat_free_context(formatContext);
    formatContext = nullptr;
  }
  if (videoCodecContext) {
    avcodec_free_context(&videoCodecContext);
  }
  videoCodec=nullptr;
  videoStream=nullptr;
  videoCodecParams=nullptr;
  if (audioCodecContext) {
    avcodec_free_context(&audioCodecContext);
  }
  audioCodec=nullptr;
  audioStream=nullptr;
  audioCodecParams=nullptr;
  return true;
}