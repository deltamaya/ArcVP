//
// Created by delta on 1 Oct 2024.
//
#include "video-reader.hh"


bool VideoReader::firstVideoFrame = true;

bool VideoReader::rescaleVideoFrame() {
  std::unique_lock lkFrame{mutexVideoFrame_};
  std::uint8_t *dest[3] = {videoFrameBuffer_.data(), videoFrameBuffer_.data() + frameWidth * frameHeight,
                           videoFrameBuffer_.data() + 2 * frameWidth * frameHeight};
  int destLinesize[3] = {frameWidth, frameWidth / 2, frameWidth / 2};
  if (!swsContext_) {
    swsContext_ = sws_getContext(curVideoFrame_->width,
                                 curVideoFrame_->height,
                                 videoCodecContext_->pix_fmt,
                                 frameWidth,
                                 frameHeight,
                                 AV_PIX_FMT_YUV420P,
                                 SWS_BILINEAR,
                                 nullptr,
                                 nullptr,
                                 nullptr);
    if (!swsContext_) {
      spdlog::error("Unable to create swsContext: {}");
      return false;
    }
  }
  sws_scale(swsContext_,
            curVideoFrame_->data,
            curVideoFrame_->linesize,
            0,
            curVideoFrame_->height,
            dest,
            destLinesize);
  return true;
}

bool VideoReader::tryReceiveVideoFrame() {
  int ret;
  {
    std::unique_lock lkFrame{mutexVideoFrame_};
    if (curVideoFrame_) {
      av_frame_unref(curVideoFrame_);
    }
    ret = avcodec_receive_frame(videoCodecContext_, curVideoFrame_);
  }

  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return false;
  }
  if (ret < 0) {
    spdlog::error("Unable to receive video frame: {}", av_err2str(ret));
    return false;
  }
  return true;
}


bool VideoReader::chooseFile() {
  auto f = pfd::open_file("Choose video to play", pfd::path::home(),
                          {
                              "All Files", "*"},
                          pfd::opt::none);
  if (f.result().size() < 1) {
    pfd::message msg("Notice", "Please choose a video to play.", pfd::choice::ok);
    return false;
  }
  if (!open(f.result().front().c_str())) {
    spdlog::error("Unable to open file: {}", f.result().front().c_str());
    pfd::message msg("Error", "Please choose a supported video format.", pfd::choice::ok);
    return false;
  }
  return true;
}

void VideoReader::close() {
  spdlog::info("Closing video file");

  if (playbackThread_) {
    // join playback thread
    playing_.clear();
    playbackThread_->join();
    playbackThread_ = nullptr;
    spdlog::info("playback thread joined");
  }

  if (audioDeviceId > 0) {
    SDL_CloseAudioDevice(audioDeviceId);
    spdlog::info("Closing audio device: {}", audioDeviceName);
    audioDeviceId = -1;
    audioDeviceName = nullptr;
  }

  if (decodeThread_) {
    // join decode thread
    decoding_.clear();
    while (!videoPacketQueue_.empty()) {
      av_packet_unref(videoPacketQueue_.front());
      videoPacketQueue_.pop();
    }
    while (!audioPacketQueue_.empty()) {
      av_packet_unref(audioPacketQueue_.front());
      audioPacketQueue_.pop();
    }
    videoPacketQueueCanProduce_.notify_all();
    audioPacketQueueCanProduce_.notify_all();
    decodeThread_->join();
    decodeThread_ = nullptr;
    spdlog::info("decode thread joined");
  }

  if (curTexture_) {
    SDL_DestroyTexture(curTexture_);
    curTexture_ = nullptr;
  }
  if (curRenderer) {
    SDL_DestroyRenderer(curRenderer);
    curRenderer = nullptr;
  }
  if (swsContext_) {
    sws_freeContext(swsContext_);
    swsContext_ = nullptr;
  }
  if (swrContext_) {
    swr_free(&swrContext_);
  }
  if (formatContext_) {
    avformat_close_input(&formatContext_);
  }
  if (videoCodecContext_) {
    avcodec_free_context(&videoCodecContext_);
  }
  if (audioCodecContext_) {
    avcodec_free_context(&audioCodecContext_);
  }
  resetSpeed();
  spdlog::info("video closed");
}

