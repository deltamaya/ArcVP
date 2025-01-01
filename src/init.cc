//
// Created by delta on 1 Oct 2024.
//
#include "imgui.h"
#include "video-reader.hh"
extern "C"{
#include <libavutil/error.h>
  }

bool VideoReader::open(const char *filename) {
  formatContext_ = avformat_alloc_context();
  int ret;

  if (ret = avformat_open_input(&formatContext_, filename, nullptr, nullptr),
      ret) {
    spdlog::error("Unable to open video file: {}", av_err2str(ret));
    return false;
  }
  if (ret = avformat_find_stream_info(formatContext_, nullptr), ret < 0) {
    spdlog::error("Could not find stream information: {}", av_err2str(ret));
    return false;
  }
  spdlog::info("Opening file: {} with {} streams", filename,
               formatContext_->nb_streams);
  // Find video and audio streams
  if (!findAVStreams()) {
    spdlog::error("Unable to find any valid stream");
    return false;
  }
  if (videoCodecParams_) {
    frameWidth = videoCodecParams_->width;
    frameHeight = videoCodecParams_->height;
  }

  // Set up the video decoder context
  if (!setupVideoCodecContext()) {
    spdlog::error("Unable to setup video decoder context");
    return false;
  }
  // Set up the audio decoder context
  if (!setupAudioCodecContext()) {
    spdlog::error("Unable to setup audio decoder context");
    return false;
  }
  av_dump_format(formatContext_, videoStreamIndex_, filename, 0);
  if (audioCodecContext_) {
    audioSampleRate_ = audioCodecContext_->sample_rate;
  }
  videoFrameBuffer_.resize(frameWidth * frameHeight * 3);
  {
    // std::unique_lock lkWindow{mutexWindow_};
    curRenderer = SDL_CreateRenderer(curWindow, -1, SDL_RENDERER_ACCELERATED);
    int width, height;
    SDL_GetWindowSize(curWindow, &width, &height);
    SDL_RenderSetLogicalSize(curRenderer, width, height);
    curTexture_ =
        SDL_CreateTexture(curRenderer, SDL_PIXELFORMAT_YV12,
                          SDL_TEXTUREACCESS_STREAMING, frameWidth, frameHeight);
    // SDL_GetRendererOutputSize(curRenderer, &width, &height);

    destRect_ = {0, 0, width, height};
  }
  SDL_GetDefaultAudioInfo(&audioDeviceName, &audioSpec, false);
  spdlog::info("default audio device: {}", audioDeviceName);
  if (!setupAudioDevice(audioCodecContext_->sample_rate)) {
    return false;
  }
  firstAudioFrame = true;
  firstVideoFrame = true;
  playbackSpeed = 1.0;
  return true;
}

bool VideoReader::setupAudioDevice(std::int64_t sampleRate) {
  SDL_AudioSpec targetSpec;
  // Set audio settings from codec info
  targetSpec.freq = sampleRate;
  targetSpec.format = AUDIO_F32;
  targetSpec.channels = audioCodecContext_->ch_layout.nb_channels;
  targetSpec.silence = 0;
  targetSpec.samples = 4096;
  targetSpec.callback = audioCallback;
  targetSpec.userdata = this;
  audioDeviceId = SDL_OpenAudioDevice(audioDeviceName, false, &targetSpec,
                                      &audioSpec, false);
  if (audioDeviceId <= 0) {
    spdlog::error("SDL_OpenAudio: {}", SDL_GetError());
    return false;
  }
  return true;
}

bool VideoReader::setupVideoCodecContext() {
  int ret = 0;
  if (videoStreamIndex_ == -1) {
    spdlog::error("Unable to find valid video stream");
    return false;
  } else {
    videoCodecContext_ = avcodec_alloc_context3(videoCodec_);
    if (!videoCodecContext_) {
      spdlog::error("Unable to allocate video codec context");
      std::exit(1);
    }
    if (ret = avcodec_parameters_to_context(videoCodecContext_,
                                            videoCodecParams_),
        ret < 0) {
      spdlog::error("Unable to initialize video codec context: {}",
                    av_err2str(ret));
      return false;
    }
    if (ret = avcodec_open2(videoCodecContext_, videoCodec_, nullptr),
        ret < 0) {
      spdlog::error("Unable to open video codec: {}", av_err2str(ret));
      return false;
    }
  }
  return true;
}

bool VideoReader::setupAudioCodecContext() {
  int ret = 0;
  if (audioStreamIndex_ == -1) {
    spdlog::error("Unable to find valid audio stream");
    return false;
  }

  audioCodecContext_ = avcodec_alloc_context3(audioCodec_);
  if (!audioCodecContext_) {
    spdlog::error("Unable to allocate audio codec context");
    std::exit(1);
  }
  if (ret =
          avcodec_parameters_to_context(audioCodecContext_, audioCodecParams_),
      ret < 0) {
    spdlog::error("Unable to initialize audio codec context: {}",
                  av_err2str(ret));
    return false;
  }
  if (ret = avcodec_open2(audioCodecContext_, audioCodec_, nullptr), ret < 0) {
    spdlog::error("Unable to open audio codec: {}", av_err2str(ret));
    return false;
  }

  return true;
}

bool VideoReader::findAVStreams() {
  const AVCodec *codec;
  videoStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_VIDEO,
                                          -1, -1, &codec, 0);
  if (videoStreamIndex_ <= -1) {
    spdlog::error("Video stream not found: {}", av_err2str(videoStreamIndex_));
    std::exit(1);
  }
  const auto stream = formatContext_->streams[videoStreamIndex_];
  AVCodecParameters *params = stream->codecpar;
  videoCodec_ = codec;
  videoCodecParams_ = params;
  videoStreamTimebase_ = stream->time_base;
  averageFrameRate_ = stream->avg_frame_rate;
  durationMilli =
      stream->duration * stream->time_base.num * 1000. / stream->time_base.den;
  if (durationMilli == AV_NOPTS_VALUE) {
    spdlog::info("logging all durations: ");
    for (int i = 0; i < formatContext_->nb_streams; i++) {
      auto duration = formatContext_->streams[i]->duration;
      auto timeBase = formatContext_->streams[i]->time_base;
      spdlog::info("index: {}, duration: {}ms", i, duration);
      if (duration != AV_NOPTS_VALUE) {
        durationMilli = duration * 1000. * timeBase.num / timeBase.den;
        break;
      }
    }

    if (durationMilli == AV_NOPTS_VALUE) {
      spdlog::error("Unable to find video duration!");
      std::exit(1);
    }
    spdlog::debug("Video stream found: \n[\n\tCodec: {}\n\tAverage Frame Rate: "
                  "{}/{}\n\tHeight: {}\n\tWidth: {}\n\tDuration: {}\n]",
                  codec->long_name, stream->avg_frame_rate.num,
                  stream->avg_frame_rate.den, params->height, params->width,
                  durationMilli);
  }
  audioStreamIndex_ = av_find_best_stream(formatContext_, AVMEDIA_TYPE_AUDIO,
                                          -1, -1, &codec, 0);
  if (audioStreamIndex_ <= -1) {
    spdlog::info("Audio stream not found: {}", av_err2str(audioStreamIndex_));
  } else {
    const auto curStream = formatContext_->streams[audioStreamIndex_];
    params = curStream->codecpar;
    audioCodec_ = codec;
    audioCodecParams_ = params;
    audioStreamTimebase_ = curStream->time_base;
    audioSampleRate_ = params->sample_rate;
    spdlog::debug("Audio stream found: \n[\n\tCodec: {}\n\tSample Rate: "
                  "{}\n\tChannel: {}\n]",
                  codec->long_name, params->sample_rate,
                  params->ch_layout.nb_channels);
  }
  return videoStreamIndex_ != -1 || audioStreamIndex_ != -1;
}

void VideoReader::setWindow(SDL_Window *window) { curWindow = window; }
