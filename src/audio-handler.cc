//
// Created by delta on 1 Oct 2024.
//
#include "video-reader.hh"

bool VideoReader::firstAudioFrame = true;

bool VideoReader::resampleAudioFrame() {
  std::unique_lock lkFrame{mutexAudioFrame_};
  if (!swrContext_) {
    swr_alloc_set_opts2(&swrContext_,
                        &audioCodecParams_->ch_layout,
                        AV_SAMPLE_FMT_FLT,
                        audioCodecParams_->sample_rate,
                        &audioCodecParams_->ch_layout,
                        audioCodecContext_->sample_fmt,
                        audioCodecParams_->sample_rate,
                        0,
                        nullptr);

    swr_init(swrContext_);
  }
  uint8_t **converted_data = nullptr;
  int max_samples = audioCodecContext_->frame_size;
  av_samples_alloc_array_and_samples(&converted_data, nullptr, 2, max_samples, AV_SAMPLE_FMT_FLT, 0);
  if (!converted_data) {
    spdlog::error("Unable to alloc sample array");
    std::exit(1);
  }
  int outSamples = av_rescale_rnd(swr_get_delay(swrContext_, audioCodecContext_->sample_rate) +
                                  curAudioFrame_->nb_samples,
                                  audioCodecContext_->sample_rate,
                                  audioCodecContext_->sample_rate,
                                  AV_ROUND_UP);

  audioFrameBuffer_.resize(outSamples * audioCodecContext_->ch_layout.nb_channels * sizeof(float));
  uint8_t *outBuffer = audioFrameBuffer_.data();
  swr_convert(swrContext_,
              &outBuffer,
              outSamples,
              (const uint8_t **) curAudioFrame_->data,
              curAudioFrame_->nb_samples);
  return true;
}

bool VideoReader::tryReceiveAudioFrame() {
  int ret;
  if (curAudioFrame_) {
    av_frame_unref(curAudioFrame_);
  }
  {
    ret = avcodec_receive_frame(audioCodecContext_, curAudioFrame_);
  }
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return false;
  } else if (ret < 0) {
    spdlog::error("Unable to receive audio packet: {}", av_err2str(ret));
    return false;
  }
  return true;
}


void VideoReader::audioSyncTo(std::int64_t bytesPlayed) {
  std::int64_t expectedAudioPlayTime = (SDL_GetTicks64() - audioEntryPoint) * 1. / playbackSpeed;
  std::int64_t realAudioPlayTime = getAudioPlayTime(bytesPlayed);
  spdlog::debug("expectedAudioPlayTime: {}, realAudioPlayTime: {}", expectedAudioPlayTime, realAudioPlayTime);
  std::int64_t deltaTime = std::abs(expectedAudioPlayTime - realAudioPlayTime);
  if (deltaTime < AUDIO_SYNC_THRESHOLD) {
    return;
  }
  std::int64_t maxSamplesToOperate = deltaTime * audioSampleRate_ / 1000.;
  if (realAudioPlayTime > expectedAudioPlayTime) {
    // adding samples to slow down audio
  } else {
    // removing samples to speed up audio
    audioFrameBuffer_.erase(
        std::remove_if(
            begin(audioFrameBuffer_) + audioCurBufferPos_,
            end(audioFrameBuffer_),
            [=, i = 0, cnt = 0](std::uint8_t)mutable {
              if (cnt >= maxSamplesToOperate)return false;
              if (i++ & 1) {
                cnt++;
                return true;
              }
              return false;
            }
        ),
        end(audioFrameBuffer_)
    );
  }
}

std::int64_t VideoReader::getAudioPlayTime(std::int64_t bytesPlayed) {
  // use sizeof(std::int??_t) in case you use integer sample format
  std::int64_t bytesPerSample = audioCodecParams_->ch_layout.nb_channels * sizeof(float);
  double samplePlayed = double(bytesPlayed) / bytesPerSample;
  return ((1000. * (samplePlayed / audioSampleRate_)) + playbackOffset_);
}