//
// Created by delta on 5/8/2025.
//
#include "player.h"
namespace ArcVP {
void Player::audioDecodeThreadWorker() {
  while (true) {
    std::unique_lock sync_lock{sync_state_.mtx_};
    while (audio_decode_worker_status_ == WorkerStatus::Idle) {
      std::this_thread::sleep_for(10ms);
    }
    if (audio_decode_worker_status_ == WorkerStatus::Exiting) {
      goto end;
    }
    int64_t played_ms = getPlayedMs();
    AVFrame* frame = av_frame_alloc();
    int ret = 0;
    while (true) {
      std::scoped_lock audioLock{media_context_.audio_codec_mtx_};
      ret = avcodec_receive_frame(media_context_.audio_codec_context_, frame);
      if (ret == 0) {
        break;
      }
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        // spdlog::debug("audio thread receive packet with lock");
        auto pkt = audio_packet_channel_.receive();
        // spdlog::debug("packet acquired");
        if (!pkt) {
          spdlog::info("Audio Consumer exited due to closed channel");
          goto end;
        }
        ret = avcodec_send_packet(media_context_.audio_codec_context_,
                                  pkt.value());
        if (ret < 0) {
          spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
        }
        av_packet_free(&pkt.value());
      } else {
        spdlog::error("Unable to receive audio frame: {}", av_err2str(ret));
        av_frame_free(&frame);
      }
    }

    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms =
        ptsToTime(frame->pts, media_context_.audio_stream_->time_base);

    {
      // spdlog::debug("lock done");
      if (present_ms < played_ms ||
          !audio_frame_channel_.empty() &&
              audio_frame_channel_.back()->present_ms >= present_ms) {
        spdlog::info("Video: Dropped frame at {}s", present_ms / 1000.);
        av_frame_free(&frame);
        continue;
      }
    }

    // spdlog::debug("audio try acquire empty semaphore");
    // spdlog::debug("semaphore acquired");

    audio_frame_channel_.send({frame, present_ms});
  }
end:
  spdlog::info("Audio Decoder Thread Exited");
}

bool Player::resampleAudioFrame(AVFrame* frame) {
  static SwrContext* ctx = nullptr;
  if (!ctx) {
    swr_alloc_set_opts2(
        &ctx, &media_context_.audio_codec_params_->ch_layout, AV_SAMPLE_FMT_FLT,
        media_context_.audio_codec_params_->sample_rate,
        &media_context_.audio_codec_params_->ch_layout,
        media_context_.audio_codec_context_->sample_fmt,
        media_context_.audio_codec_params_->sample_rate, 0, nullptr);

    swr_init(ctx);
  }
  int outSamples = av_rescale_rnd(
      swr_get_delay(ctx, media_context_.audio_codec_context_->sample_rate) +
          frame->nb_samples,
      media_context_.audio_codec_context_->sample_rate,
      media_context_.audio_codec_context_->sample_rate, AV_ROUND_UP);

  audio_buffer_.resize(
      outSamples * media_context_.audio_codec_context_->ch_layout.nb_channels *
      sizeof(float));
  uint8_t* outBuffer = audio_buffer_.data();
  swr_convert(ctx, &outBuffer, outSamples, frame->data, frame->nb_samples);
  return true;
}

void audioCallback(void* userdata, Uint8* stream, int len) {
  auto arc = static_cast<Player*>(userdata);
  static int64_t audioPos = 0;

  while (len > 0) {
    int bytesCopied = 0;
    if (audioPos >= arc->audio_buffer_.size()) {
      AVFrame* frame = arc->tryFetchAudioFrame();
      if (!frame) {
        spdlog::info("AudioCallback: no available frame");
        return;
      }

      audioPos = 0;
      arc->resampleAudioFrame(frame);
      arc->audioSyncTo(frame);
    }
    bytesCopied = std::min(arc->audio_buffer_.size() - audioPos,
                           static_cast<std::size_t>(len));
    memcpy(stream, arc->audio_buffer_.data() + audioPos, bytesCopied);
    len -= bytesCopied;
    stream += bytesCopied;
    audioPos += bytesCopied;
    std::scoped_lock sync_lock{arc->sync_state_.mtx_};
    arc->sync_state_.sample_count_ += (bytesCopied) / sizeof(float);
  }
}

bool Player::setupAudioDevice(int sampleRate) {
  SDL_AudioSpec targetSpec;
  // Set audio settings from codec info
  targetSpec.freq = sampleRate;
  targetSpec.format = AUDIO_F32;
  targetSpec.channels =
      media_context_.audio_codec_context_->ch_layout.nb_channels;
  targetSpec.silence = 0;
  targetSpec.samples = 4096;
  targetSpec.callback = audioCallback;
  targetSpec.userdata = this;
  audio_device_.id = SDL_OpenAudioDevice(audio_device_.name, false, &targetSpec,
                                         &audio_device_.spec, false);
  if (audio_device_.id <= 0) {
    spdlog::error("SDL_OpenAudio: {}", SDL_GetError());
    return false;
  }

  return true;
}

constexpr int AUDIO_SYNC_THRESHOLD = 100;

void Player::audioSyncTo(AVFrame* frame) {
  int64_t elapsed_ms = getPlayedMs();
  int64_t presentTimeMs =
      ptsToTime(frame->pts, media_context_.audio_stream_->time_base);
  std::int64_t deltaTime = presentTimeMs - elapsed_ms;
  // spdlog::debug("Audio: deltaTime: {}ms",deltaTime);
  if (std::abs(deltaTime) < AUDIO_SYNC_THRESHOLD) {
    return;
  }
  if (deltaTime > 0) {
    int64_t samples =
        deltaTime * media_context_.audio_codec_params_->sample_rate / 1000.;
    while (samples--) {
      audio_buffer_.push_back(0);
    }
    spdlog::info("Audio: Sync to: {}ms", deltaTime);
  } else {
    auto sampleRate = media_context_.audio_codec_params_->sample_rate;
    int64_t samples = std::abs(deltaTime) * sampleRate / 1000.;
    auto end = samples > audio_buffer_.size() ? audio_buffer_.end()
                                              : audio_buffer_.begin() + samples;
    audio_buffer_.erase(audio_buffer_.begin(), end);
    spdlog::info("Audio: Sync to: {}ms", deltaTime);
  }
}
}  // namespace ArcVP