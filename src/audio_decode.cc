//
// Created by delta on 5/8/2025.
//
#include "player.h"
namespace ArcVP {
void Player::audioDecodeThreadWorker() {
  while (true) {
    std::unique_lock status_lock{audio_decode_worker_.mtx};
    audio_decode_worker_.cv.wait(status_lock, [this] {
      return audio_decode_worker_.status != WorkerStatus::Idle;
    });
    if (audio_decode_worker_.status == WorkerStatus::Exiting) {
      break;
    }
    AVFrame* frame = av_frame_alloc();
    int ret = 0;
    while (true) {
      ret = avcodec_receive_frame(media_.audio_codec_context_, frame);
      if (ret == 0) {
        break;
      }
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        // spdlog::debug("audio thread receive packet with lock");
        auto pkt = audio_decode_worker_.packet_chan.front();
        audio_decode_worker_.packet_chan.pop_front();
        // spdlog::debug("packet acquired");
        if (!pkt) {
          spdlog::info("Audio Consumer exited due to closed channel");
          audio_decode_worker_.status = WorkerStatus::Exiting;
          goto end;
        }
        ret = avcodec_send_packet(media_.audio_codec_context_,
                                  pkt);
        if (ret < 0) {
          spdlog::error("Error sending packet to codec: {}", av_err2str(ret));
        }
        av_packet_free(&pkt);
      } else {
        spdlog::error("Unable to receive audio frame: {}", av_err2str(ret));
        av_frame_free(&frame);
      }
    }

    // spdlog::debug("audio try lock audio queue");
    int64_t present_ms =
        ptsToTime(frame->pts, media_.audio_stream_->time_base);
    // SDL 会从 stream 中取数据
    resampleAudioFrame(frame);

    while (SDL_GetAudioStreamAvailable(audio_stream)>114514) {
      std::this_thread::sleep_for(10ms);
    }

      SDL_PutAudioStreamData(audio_stream,audio_buffer_.data(),audio_buffer_.size());
      SDL_FlushAudioStream(audio_stream);
    sync_state_.sample_count_ +=
        audio_buffer_.size() / sizeof(float) /
        media_.audio_codec_params_->ch_layout.nb_channels;
  }
end:
  spdlog::info("Audio decode thread exited");
}

bool Player::resampleAudioFrame(AVFrame* frame) {
  static SwrContext* ctx = nullptr;
  if (!ctx) {
    swr_alloc_set_opts2(
        &ctx, &media_.audio_codec_params_->ch_layout, AV_SAMPLE_FMT_FLT,
        media_.audio_codec_params_->sample_rate,
        &media_.audio_codec_params_->ch_layout,
        media_.audio_codec_context_->sample_fmt,
        media_.audio_codec_params_->sample_rate, 0, nullptr);

    swr_init(ctx);
  }
  int outSamples = av_rescale_rnd(
      swr_get_delay(ctx, media_.audio_codec_context_->sample_rate) +
          frame->nb_samples,
      media_.audio_codec_context_->sample_rate,
      media_.audio_codec_context_->sample_rate, AV_ROUND_UP);

  audio_buffer_.resize(
      outSamples * media_.audio_codec_context_->ch_layout.nb_channels *
      sizeof(float));
  uint8_t* outBuffer = audio_buffer_.data();
  swr_convert(ctx, &outBuffer, outSamples, frame->data, frame->nb_samples);
  return true;
}

void audioCallback(void* userdata, SDL_AudioStream* stream, int additional_amount,int total_amount) {
  auto arc = static_cast<Player*>(userdata);
  static int64_t audioPos = 0;
  std::memset(stream,0,total_amount);
  // while (len > 0) {
  //   int bytesCopied = 0;
  //   if (audioPos >= arc->audio_buffer_.size()) {
  //     auto [frame,present_ms] = arc->tryFetchAudioFrame();
  //     if (!frame) {
  //       spdlog::info("AudioCallback: no available frame");
  //       return;
  //     }
  //     // auto t =
  //     //     ptsToTime(frame->pts,
  //     //     arc->media_context_.audio_stream_->time_base);
  //     // spdlog::debug("playing audio frame: {}ms",t);
  //
  //     audioPos = 0;
  //     arc->resampleAudioFrame(frame);
  //     int64_t played_ms=arc->getPlayedMs();
  //     int64_t delta_time=present_ms-played_ms;
  //     if (delta_time>50) {
  //       int64_t count=(delta_time/1000.)*arc->media_.audio_codec_params_->sample_rate*2*4;
  //       arc->audio_buffer_.insert(arc->audio_buffer_.begin(),count,0);
  //     }
  //     // arc->audioSyncTo(frame,present_ms);
  //   }
  //   bytesCopied = std::min(arc->audio_buffer_.size() - audioPos,
  //                          static_cast<std::size_t>(len));
  //   memcpy(stream, arc->audio_buffer_.data() + audioPos, bytesCopied);
  //   len -= bytesCopied;
  //   stream += bytesCopied;
  //   audioPos += bytesCopied;
  //   std::scoped_lock sync_lock{arc->sync_state_.mtx_};
  //   arc->sync_state_.sample_count_ +=
  //       bytesCopied / sizeof(float) /
  //       arc->media_.audio_codec_params_->ch_layout.nb_channels;
  // }
}

bool Player::setupAudioDevice() {
  spdlog::debug("checking if has errors: {}",SDL_GetError());

  audio_device_.id= SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,nullptr);

  if (audio_device_.id == 0) {
    return false;
  }
  SDL_GetAudioDeviceFormat(audio_device_.id,&audio_device_.spec,nullptr);
  audio_device_.name=SDL_GetAudioDeviceName(audio_device_.id);
  if (!audio_device_.name) {
    spdlog::error("Unable to get audio device name: {}",SDL_GetError());
  }
  return true;
}

constexpr int AUDIO_SYNC_THRESHOLD = 100;

void Player::audioSyncTo(AVFrame* frame, int64_t present_ms) {
  std::scoped_lock lk{sync_state_.mtx_};
  int64_t played_ms=getPlayedMs();
  std::int64_t deltaTime = present_ms - played_ms;
  // spdlog::debug("Audio: deltaTime: {}ms",deltaTime);
  if (std::abs(deltaTime) < AUDIO_SYNC_THRESHOLD) {
    return;
  }
  if (deltaTime > 0) {
    int64_t samples =
        deltaTime * media_.audio_codec_params_->sample_rate / 1000.;
    while (samples--) {
      audio_buffer_.push_back(0);
    }
    spdlog::info("Audio: Sync to: {}ms", deltaTime);
  } else {
    auto sampleRate = media_.audio_codec_params_->sample_rate;
    int64_t samples = std::abs(deltaTime) * sampleRate / 1000.;
    auto end = samples > audio_buffer_.size() ? audio_buffer_.end()
                                              : audio_buffer_.begin() + samples;
    audio_buffer_.erase(audio_buffer_.begin(), end);
    spdlog::info("Audio: Sync to: {}ms", deltaTime);
  }
}
}  // namespace ArcVP