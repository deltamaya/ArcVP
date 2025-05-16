//
// Created by delta on 1/22/2025.
//

#include "player.h"

namespace ArcVP {
std::tuple<int, int> findAVStream(AVFormatContext *formatContext) {
  int videoStreamIndex = -1, audioStreamIndex = -1;
  videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1,
                                         -1, nullptr, 0);
  audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1,
                                         -1, nullptr, 0);
  return std::make_tuple(videoStreamIndex, audioStreamIndex);
}

bool Player::open(const char *filename) {
  std::scoped_lock lk{media_.format_mtx_,
                      media_.video_codec_mtx_,
                      media_.audio_codec_mtx_};
  // open file and find stream info
  AVFormatContext *formatContext = nullptr;
  int ret = avformat_open_input(&formatContext, filename, nullptr, nullptr);
  if (ret != 0) {
    spdlog::error("Unable to open file '{}': {}", filename, av_err2str(ret));
    return false;
  }
  ret = avformat_find_stream_info(formatContext, nullptr);
  if (ret != 0) {
    spdlog::error("Unable to find stream info: {}", av_err2str(ret));
    return false;
  }

  // find video & audio stream
  auto [videoStreamIndex, audioStreamIndex] = findAVStream(formatContext);
  bool hasVideo = true, hasAudio = true;

  if (videoStreamIndex == -1) {
    spdlog::info("Unable to find video stream");
    hasVideo = false;
  }

  if (audioStreamIndex == -1) {
    spdlog::info("Unable to find audio stream");
    hasAudio = false;
  }

  // setup video codec
  const AVStream *videoStream = nullptr, *audioStream = nullptr;
  const AVCodecParameters *videoCodecParams = nullptr,
                          *audioCodecParams = nullptr;
  const AVCodec *videoCodec = nullptr, *audioCodec = nullptr;

  if (hasVideo) {
    videoStream = formatContext->streams[videoStreamIndex];
    videoCodecParams = videoStream->codecpar;
    videoCodec = avcodec_find_decoder(videoCodecParams->codec_id);
    if (videoCodec == nullptr) {
      spdlog::error("Unable to find video codec");
      return false;
    }
  }

  // setup audio codec
  if (hasAudio) {
    audioStream = formatContext->streams[audioStreamIndex];
    audioCodecParams = audioStream->codecpar;
    audioCodec = avcodec_find_decoder(audioCodecParams->codec_id);
    if (audioCodec == nullptr) {
      spdlog::error("Unable to find video codec");
      return false;
    }
  }

  // setup codec context
  AVCodecContext *videoCodecContext = nullptr, *audioCodecContext = nullptr;
  if (hasVideo) {
    videoCodecContext = avcodec_alloc_context3(videoCodec);
    if (!videoCodecContext) {
      spdlog::error("Unable to allocate video codec context");
      std::exit(1);
    }
    if (ret =
            avcodec_parameters_to_context(videoCodecContext, videoCodecParams),
        ret < 0) {
      spdlog::error("Unable to initialize video codec context: {}",
                    av_err2str(ret));
      return false;
    }
    if (ret = avcodec_open2(videoCodecContext, videoCodec, nullptr), ret < 0) {
      spdlog::error("Unable to open video codec: {}", av_err2str(ret));
      return false;
    }
    this->width = videoCodecParams->width;
    this->height = videoCodecParams->height;
  }

  if (hasAudio) {
    audioCodecContext = avcodec_alloc_context3(audioCodec);
    if (!audioCodecContext) {
      spdlog::error("Unable to allocate audio codec context");
      std::exit(1);
    }
    if (ret =
            avcodec_parameters_to_context(audioCodecContext, audioCodecParams),
        ret < 0) {
      spdlog::error("Unable to initialize audio codec context: {}",
                    av_err2str(ret));
      return false;
    }
    if (ret = avcodec_open2(audioCodecContext, audioCodec, nullptr), ret < 0) {
      spdlog::error("Unable to open audio codec: {}", av_err2str(ret));
      return false;
    }
  }

  this->media_.format_context_ = formatContext;

  this->media_.video_stream_ = videoStream;
  this->media_.video_stream_index_ = videoStreamIndex;
  this->media_.video_codec_ = videoCodec;
  this->media_.video_codec_params_ = videoCodecParams;
  this->media_.video_codec_context_ = videoCodecContext;

  this->media_.audio_stream_ = audioStream;
  this->media_.audio_stream_index_ = audioStreamIndex;
  this->media_.audio_codec_ = audioCodec;
  this->media_.audio_codec_params_ = audioCodecParams;
  this->media_.audio_codec_context_ = audioCodecContext;

  spdlog::info("Opened file '{}'", filename);
  return true;
}

void Player::close() {
  std::scoped_lock lk{sync_state_.mtx_};
  pause();
  sync_state_.status_ = InstanceStatus::Idle;
  sync_state_.sample_count_ = 0;

  packet_decode_worker_.status = WorkerStatus::Idle;
  audio_decode_worker_.status = WorkerStatus::Idle;
  video_decode_worker_.status = WorkerStatus::Idle;

  // video_frame_queue_.clear();
  // audio_frame_queue_.clear();
  // // waits for the worker to put packet into the channel
  // video_packet_channel_.clear();
  // audio_packet_channel_.clear();

  {
    std::scoped_lock media_lock{media_.format_mtx_,
                                media_.video_codec_mtx_,
                                media_.audio_codec_mtx_};
    if (media_.format_context_) {
      avformat_free_context(media_.format_context_);
    }
    this->media_.format_context_ = nullptr;

    this->media_.video_stream_ = nullptr;
    this->media_.video_stream_index_ = -1;
    this->media_.video_codec_ = nullptr;
    this->media_.video_codec_params_ = nullptr;
    if (media_.video_codec_context_) {
      avcodec_free_context(&media_.video_codec_context_);
    }
    this->media_.video_codec_context_ = nullptr;

    this->media_.audio_stream_ = nullptr;
    this->media_.audio_stream_index_ = -1;
    this->media_.audio_codec_ = nullptr;
    this->media_.audio_codec_params_ = nullptr;
    if (media_.audio_codec_context_) {
      avcodec_free_context(&media_.audio_codec_context_);
    }
    this->media_.audio_codec_context_ = nullptr;
  }

  spdlog::info("Closed Input");
}
}  // namespace ArcVP