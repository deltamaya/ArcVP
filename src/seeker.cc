//
// Created by delta on 1 Oct 2024.
//

#include "video-reader.hh"

bool VideoReader::seekFrame(std::int64_t seekPlayTime) {
  if (seekPlayTime < 0 || seekPlayTime > durationMilli) {
    spdlog::error("invalid time to seek: {}", seekPlayTime);
    return false;
  }
  if (!curVideoFrame_ || videoStreamIndex_ < 0 || !formatContext_) {
    spdlog::error("Invalid state: frame, stream index, or format context not initialized.");
    return false;
  }
  int ret;
  std::int64_t timestamp = seekPlayTime * videoStreamTimebase_.den / 1000. / videoStreamTimebase_.num;
//  int seekFlag = seekPlayTime < curPlayTime ? AVSEEK_FLAG_BACKWARD : 0;
  int seekFlag = AVSEEK_FLAG_BACKWARD;
  spdlog::info("Seek to playtime: {}ms, at PTS: {}. Current: {}ms", seekPlayTime, timestamp, videoPlayTimeMilli);
  std::unique_lock lkFormat{mutexFormat_};
  playbackOffset_ += (seekPlayTime - videoPlayTimeMilli);
  if (videoStreamIndex_ >= 0) {
    std::unique_lock lkCodec{mutexVideoCodec_};
    std::unique_lock lkVideo{mutexVideoPacketQueue_};
    avcodec_flush_buffers(videoCodecContext_);
    ret = av_seek_frame(formatContext_, videoStreamIndex_, timestamp, seekFlag);
    if (ret < 0) {
      spdlog::error("Error during seeking video stream: {}", ret);
      return false;
    }
    {
      while (!videoPacketQueue_.empty()) {
        av_packet_free(&videoPacketQueue_.front());
        videoPacketQueue_.pop();
      }
    }
    firstVideoFrame = true;
    videoPacketQueueCanProduce_.notify_all();
  }
  if (audioStreamIndex_ >= 0) {
    std::unique_lock lkCodec{mutexAudioCodec_};
    std::unique_lock lkAudio{mutexAudioPacketQueue_};
    avcodec_flush_buffers(audioCodecContext_);
    {
      while (!audioPacketQueue_.empty()) {
        av_packet_free(&audioPacketQueue_.front());
        audioPacketQueue_.pop();
      }
    }
    firstAudioFrame = true;
    audioPacketQueueCanProduce_.notify_all();
  }
  spdlog::info("Seek Ok, new video offset: {}", playbackOffset_.load());
  return true;
}