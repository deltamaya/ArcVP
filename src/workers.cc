//
// Created by delta on 1 Oct 2024.
//

#include "video-reader.hh"
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

void pushPlaybackFinishEvent(void *data) {
  SDL_Event event;
  event.type = SDL_USEREVENT + 3;
  event.user.data1 = data;
  SDL_PushEvent(&event);
}

void pushDecodeFinishEvent(void *data) {
  SDL_Event event;
  event.type = SDL_USEREVENT + 2;
  event.user.data1 = data;
  SDL_PushEvent(&event);
}

void pushRenderEvent(void *data) {
  SDL_Event event;
  event.type = SDL_USEREVENT;
  event.user.data1 = data;
  SDL_PushEvent(&event);
}

void pushDefaultScreenEvent(void *data) {
  SDL_Event event;
  event.type = SDL_USEREVENT + 1;
  event.user.data1 = data;
  SDL_PushEvent(&event);
}

void VideoReader::playbackThreadBody() {
  while (playing_.test()) {
    if (!pauseVideo.test()) {
      {
        std::unique_lock lkCodec{mutexVideoCodec_};
//        spdlog::debug("video codec mutex acquired");

        if (!tryReceiveVideoFrame()) {

          {
            std::unique_lock lkVideo{mutexVideoPacketQueue_};
//            spdlog::debug("video packet queue mutex acquired, size: {}", videoPacketQueue_.size());
            videoPacketQueueCanConsume_.wait(
                lkVideo, [this] { return !videoPacketQueue_.empty(); });
//            spdlog::debug("video packet queue can consume");
            int ret = avcodec_send_packet(videoCodecContext_,
                                          videoPacketQueue_.front());
            av_packet_unref(videoPacketQueue_.front());
            videoPacketQueue_.pop();
            if (ret < 0) {
              spdlog::error("Unable to send video packet: {}", av_err2str(ret));
            }
          }
          videoPacketQueueCanProduce_.notify_all();
          tryReceiveVideoFrame();
        }
        rescaleVideoFrame();
        spdlog::debug("rescale video frame done");

      }
      std::int64_t curTick = SDL_GetTicks64();
      std::unique_lock lkFrame{mutexVideoFrame_};
      spdlog::debug("frame mutex acquired");

      if (curVideoFrame_->pts == AV_NOPTS_VALUE) {
        spdlog::warn("Skipping frame with no pts value");
        continue;
      }
      spdlog::debug("Current video pts: {}", curVideoFrame_->pts);
      std::int64_t relativePresentTimeMilli =
          (curVideoFrame_->pts * videoStreamTimebase_.num * 1000. /
           videoStreamTimebase_.den);
      if (firstVideoFrame) {
        firstVideoFrame = false;
        videoEntryPoint =
            curTick - relativePresentTimeMilli * 1. / playbackSpeed;
        spdlog::debug("new video entry point: {}", videoEntryPoint.load());
      }
      std::int64_t absolutePresentTimeMilli =
          relativePresentTimeMilli * 1. / playbackSpeed + videoEntryPoint;
      spdlog::debug("rela: {}ms, absl: {}ms, SDL time: {}ms",
                    relativePresentTimeMilli, absolutePresentTimeMilli,
                    curTick);
      if (absolutePresentTimeMilli < curTick) {
        spdlog::warn("drop frame");
        continue;
      }
      if (absolutePresentTimeMilli > curTick) {
        std::int64_t timeout = absolutePresentTimeMilli - curTick;
        spdlog::debug("playback thread sleep for {}ms", timeout);
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
      }
      if (!playing_.test()) {
        return;
      }
      videoPlayTimeMilli = relativePresentTimeMilli;
      playbackProgress = videoPlayTimeMilli * 1. / durationMilli;
      std::string errString = SDL_GetError();
      if (!errString.empty())
        spdlog::error("playback thread report: {}", SDL_GetError());
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    {
      std::unique_lock lkWindow{mutexWindow_};
      SDL_RenderClear(curRenderer);
      int ret =
          SDL_UpdateYUVTexture(curTexture_,
                               nullptr,
                               videoFrameBuffer_.data(),
                               frameWidth,
                               videoFrameBuffer_.data() + frameWidth * frameHeight,
                               frameWidth / 2,
                               videoFrameBuffer_.data() + 2 * frameWidth * frameHeight,
                               frameWidth / 2);
      if (ret < 0) {
        spdlog::error("Unable to update yuv texture: {}", SDL_GetError());
      }

      ret = SDL_RenderCopy(curRenderer, curTexture_, nullptr, &destRect_);
      if (ret < 0) {
        spdlog::error("Unable to render yuv texture: {}", SDL_GetError());
      }
      pushRenderEvent(nullptr);
    }
    spdlog::debug("total duration: {}", formatContext_->streams[videoStreamIndex_]->duration);
    if (!decodeThread_ && videoPacketQueue_.empty()) {
      spdlog::info("playback finished, video thread quit");
      pushPlaybackFinishEvent(nullptr);
      return;
    }
  }
}

void VideoReader::decodeThreadBody() {
  AVPacket *packet = av_packet_alloc();
  int ret;
  while (true) {
    {
      std::unique_lock lkFormat{mutexFormat_};
      ret = av_read_frame(formatContext_, packet);
    }
    if (ret == AVERROR_EOF) {
      spdlog::info("decoder EOF reached");
      pushDecodeFinishEvent(nullptr);
      break;
    }
    if (ret < 0) {
      spdlog::error("Unable to read video packet: {}", ret);
      break;
    }
    if (!decoding_.test()) {
      spdlog::info("Decoding thread stopped");
      break;
    }
    if (packet->stream_index == videoStreamIndex_) {
      spdlog::debug("decoding video packet at PTS: {}", packet->pts);
      {
        std::unique_lock lkVideo{mutexVideoPacketQueue_};
        videoPacketQueueCanProduce_.wait(lkVideo,
                                         [this] {
//                                           spdlog::debug("video decoder awake: {}", videoPacketQueue_.size());
                                           return videoPacketQueue_.size() < VIDEO_PACKET_QUEUE_MAX_SIZE;
                                         });
        videoPacketQueue_.push(av_packet_alloc());
        av_packet_move_ref(videoPacketQueue_.back(), packet);
      }
      videoPacketQueueCanConsume_.notify_one();
    } else if (packet->stream_index == audioStreamIndex_) {
      spdlog::debug("decoding audio packet at PTS: {}", packet->pts);
      {
        std::unique_lock lkAudio{mutexAudioPacketQueue_};
        audioPacketQueueCanProduce_.wait(lkAudio,
                                         [this] {
//                                           spdlog::debug("audio decoder awake: {}", audioPacketQueue_.size());
                                           return audioPacketQueue_.size() < AUDIO_PACKET_QUEUE_MAX_SIZE;
                                         });
        audioPacketQueue_.push(av_packet_alloc());
        av_packet_move_ref(audioPacketQueue_.back(), packet);
      }
      audioPacketQueueCanConsume_.notify_all();
    } else {
      spdlog::info("unknown packet stream index: {}", packet->stream_index);
      av_packet_unref(packet);
    }
  }
  av_packet_unref(packet);
  // spdlog::debug("decode thread exited");
}

void VideoReader::defaultScreenThreadBody() {
  while (!playing_.test()) {
    pushDefaultScreenEvent(nullptr);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
}

void VideoReader::showNextFrame() {
  std::unique_lock lkCodec{mutexVideoCodec_};
  //        spdlog::debug("video codec mutex acquired");

  if (!tryReceiveVideoFrame()) {

    {
      std::unique_lock lkVideo{mutexVideoPacketQueue_};
      //            spdlog::debug("video packet queue mutex acquired, size: {}", videoPacketQueue_.size());
      videoPacketQueueCanConsume_.wait(
          lkVideo, [this] { return !videoPacketQueue_.empty(); });
      //            spdlog::debug("video packet queue can consume");
      int ret = avcodec_send_packet(videoCodecContext_,
                                    videoPacketQueue_.front());
      av_packet_unref(videoPacketQueue_.front());
      videoPacketQueue_.pop();
      if (ret < 0) {
        spdlog::error("Unable to send video packet: {}", av_err2str(ret));
      }
    }
    videoPacketQueueCanProduce_.notify_all();
    tryReceiveVideoFrame();
  }
  if (curVideoFrame_->pts == AV_NOPTS_VALUE) {
    lkCodec.unlock();
    showNextFrame();
    return;
  }
  rescaleVideoFrame();
  std::int64_t relativePresentTimeMilli =
      (curVideoFrame_->pts * videoStreamTimebase_.num * 1000. /
       videoStreamTimebase_.den);
  videoPlayTimeMilli = relativePresentTimeMilli;
  playbackProgress = videoPlayTimeMilli * 1. / durationMilli;
  {
    std::unique_lock lkWindow{mutexWindow_};
    SDL_RenderClear(curRenderer);
    int ret =
        SDL_UpdateYUVTexture(curTexture_,
                             nullptr,
                             videoFrameBuffer_.data(),
                             frameWidth,
                             videoFrameBuffer_.data() + frameWidth * frameHeight,
                             frameWidth / 2,
                             videoFrameBuffer_.data() + 2 * frameWidth * frameHeight,
                             frameWidth / 2);
    if (ret < 0) {
      spdlog::error("Unable to update yuv texture: {}", SDL_GetError());
    }

    ret = SDL_RenderCopy(curRenderer, curTexture_, nullptr, &destRect_);
    if (ret < 0) {
      spdlog::error("Unable to render yuv texture: {}", SDL_GetError());
    }
    pushRenderEvent(nullptr);
  }
}