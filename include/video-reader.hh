//
// Created by delta on 23 Sep 2024.
//

#ifndef ARCVEDITOR_SRC_VIDEO_READER_HH_
#define ARCVEDITOR_SRC_VIDEO_READER_HH_

#include <spdlog/spdlog.h>
#include <cstdint>
#include <vector>
#include <optional>
#include <spdlog/spdlog.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include "imgui_sdl.h"
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include <SDL2/SDL.h>
}
constexpr size_t VIDEO_PACKET_QUEUE_MAX_SIZE = 1024;
constexpr size_t AUDIO_PACKET_QUEUE_MAX_SIZE = 1024;
// milliseconds
constexpr size_t AUDIO_SYNC_THRESHOLD = 100;

constexpr size_t SDL_PLAYER_RENDER_EVENT = SDL_USEREVENT;
constexpr size_t SDL_PLAYER_DEFAULT_SCREEN_EVENT = SDL_USEREVENT + 1;
constexpr size_t SDL_PLAYER_DECODE_FINISH_EVENT = SDL_USEREVENT + 2;
constexpr size_t SDL_PLAYER_PLAYBACK_FINISH_EVENT = SDL_USEREVENT + 3;

struct VideoReader {

  AVFormatContext *formatContext_ = nullptr;
  const AVCodec *videoCodec_ = nullptr;
  const AVCodec *audioCodec_ = nullptr;
  AVCodecContext *videoCodecContext_ = nullptr;
  AVCodecContext *audioCodecContext_ = nullptr;
  AVCodecParameters *videoCodecParams_ = nullptr;
  AVCodecParameters *audioCodecParams_ = nullptr;
  AVFrame *curVideoFrame_ = nullptr;
  AVFrame *curAudioFrame_ = nullptr;

//  AVPacket *curVideoPacket_ = nullptr, *curAudioPacket_ = nullptr;
  SwsContext *swsContext_ = nullptr;
  SwrContext *swrContext_ = nullptr;
  int videoStreamIndex_ = -1;
  int audioStreamIndex_ = -1;
  int frameWidth = 0, frameHeight = 0;
  std::vector<std::uint8_t> videoFrameBuffer_, audioFrameBuffer_;
  AVRational videoStreamTimebase_, audioStreamTimebase_;
  AVRational averageFrameRate_;
  std::int64_t audioSampleRate_;
  int audioCurBufferPos_ = 0;
  std::int64_t videoPlayTimeMilli = 0;
  // time unit: millisecond
  std::atomic<std::int64_t> videoEntryPoint = 0, audioEntryPoint = 0, playbackOffset_ = 0;

  std::queue<AVPacket *> videoPacketQueue_, audioPacketQueue_;
  std::mutex mutexVideoPacketQueue_, mutexAudioPacketQueue_, mutexWindow_,
      mutexVideoCodec_, mutexAudioCodec_, mutexFormat_, mutexVideoFrame_, mutexAudioFrame_
  , mutexVideoPacket_, mutexAudioPacket_, mutexRenderer;
  std::condition_variable videoPacketQueueCanConsume_, audioPacketQueueCanConsume_;
  std::condition_variable videoPacketQueueCanProduce_, audioPacketQueueCanProduce_;
  std::atomic_flag decoding_, playing_, pauseVideo;


  static bool firstVideoFrame;
  static bool firstAudioFrame;

  SDL_Texture *curTexture_ = nullptr;
  SDL_Renderer *curRenderer = nullptr;
  // do not modify this
  SDL_Window *curWindow = nullptr;
  SDL_AudioDeviceID audioDeviceId = -1;
  SDL_AudioSpec audioSpec;
  char *audioDeviceName = nullptr;

  double playbackSpeed = 1.0;
  float playbackProgress = 0.0f;
  std::int64_t durationMilli = 0;

  std::unique_ptr<std::thread> decodeThread_ = nullptr, playbackThread_ = nullptr;
  std::unique_ptr<std::thread> defaultScreenThread_ = nullptr;

  SDL_Rect destRect_;
  int controlPanelPosX, controlPanelPosY, controlPanelSizeWidth, controlPanelSizeHeight;


  VideoReader() {
    curVideoFrame_ = av_frame_alloc();
    curAudioFrame_ = av_frame_alloc();
  }

  // only call this is main thread
  bool open(const char *filename);

  void close();

  void idle();

  void active(std::int64_t);

  bool setupAudioDevice(std::int64_t sampleRate);

  void showNextFrame();

  void startDecode() {
    decoding_.test_and_set();
    decodeThread_ = std::make_unique<std::thread>([this] { decodeThreadBody(); });
  }

  void startPlayback() {
    pauseVideo.clear();
    playing_.test_and_set();
    if (defaultScreenThread_) {
      defaultScreenThread_->join();
      defaultScreenThread_ = nullptr;
    }
    firstAudioFrame = true;
    firstVideoFrame = true;
    SDL_PauseAudioDevice(audioDeviceId, false);
    playbackThread_ = std::make_unique<std::thread>([this] { playbackThreadBody(); });
  }

  // the default screen thread will quit automatically when you call startPlayback
  void startDefaultScreen() {
    if (curRenderer) {
      SDL_DestroyRenderer(curRenderer);
      curRenderer = nullptr;
    }
    curRenderer = SDL_CreateRenderer(curWindow, -1, SDL_RENDERER_ACCELERATED);
    if (!curRenderer) {
      spdlog::error("Unable to create video renderer: {}", SDL_GetError());
      std::exit(1);
    }
    int width, height;
    SDL_GetWindowSize(curWindow, &width, &height);
    SDL_RenderSetLogicalSize(curRenderer, width, height);
    ImGuiSDL::Initialize(curRenderer, width, height);
    defaultScreenThread_ = std::make_unique<std::thread>([this] { defaultScreenThreadBody(); });
    spdlog::info("default screen thread started");
  }

  void setPlaybackSpeed(double speed);

  void setPause();

  void setUnpause();

  void togglePause();

  void playbackThreadBody();

  void defaultScreenThreadBody();

  // get audio play time in milliseconds
  std::int64_t getAudioPlayTime(std::int64_t bytesPlayed);

  void audioSyncTo(std::int64_t bytesPlayed);

  void controlPanel();

  void handleKeyDown(SDL_Event, std::int64_t);

  static void audioCallback(void *userdata, Uint8 *stream, int len) {
    auto vr = static_cast<VideoReader *>(userdata);
    static std::int64_t bytesPlayed = 0;
    auto &bufPos = vr->audioCurBufferPos_;
//    vr->audioSyncTo(bytesPlayed);
    spdlog::debug("Audio callback: total: {} len: {}", bytesPlayed, len);
    while (len > 0) {
      int bytesCopied = 0;
      if (bufPos >= vr->audioFrameBuffer_.size()) {
        /* We have already sent all our data; get more */
        bufPos -= vr->audioFrameBuffer_.size();
        vr->audioFrameBuffer_.clear();
      }
      {
        std::unique_lock lkCodec{vr->mutexAudioCodec_};
        if (!vr->tryReceiveAudioFrame()) {
          {
            std::unique_lock lkAudio{vr->mutexAudioPacketQueue_};
            vr->audioPacketQueueCanConsume_.wait(lkAudio, [vr] { return !vr->audioPacketQueue_.empty(); });
            int ret = avcodec_send_packet(vr->audioCodecContext_, vr->audioPacketQueue_.front());
            av_packet_unref(vr->audioPacketQueue_.front());
            if (ret == AVERROR(EAGAIN)) {
              spdlog::error(
                  "audio input is not accepted in the current state - must read output with avcodec_receive_frame()");
            } else if (ret == AVERROR_EOF) {
              spdlog::error("audio input EOF");
            } else if (ret < 0) {
              spdlog::error("Unable to send audio packet: {}", av_err2str(ret));
            }
            vr->audioPacketQueue_.pop();
          }
          vr->audioPacketQueueCanProduce_.notify_all();
        }
        vr->resampleAudioFrame();
      }
      auto &curBuf = vr->audioFrameBuffer_;
      bytesCopied = std::min(curBuf.size() - bufPos, static_cast<std::size_t>(len));
      memcpy(stream, curBuf.data() + bufPos, bytesCopied);
      len -= bytesCopied;
      stream += bytesCopied;
      bufPos += bytesCopied;
      bytesPlayed += bytesCopied;
      if (!vr->decodeThread_ && vr->audioPacketQueue_.empty()) {
        memset(stream, 0, len);
        spdlog::info("playback finished, audio stream quit");
        SDL_PauseAudioDevice(vr->audioDeviceId, true);
        return;
      }
    }
    if (firstAudioFrame) {
      vr->audioEntryPoint = SDL_GetTicks64() - vr->getAudioPlayTime(bytesPlayed) / vr->playbackSpeed;
      firstAudioFrame = false;
    }
  }

  bool seekFrame(std::int64_t seekPlayTime);

  void quitDefaultScreen() {
    if (defaultScreenThread_) {
      playing_.test_and_set();
      defaultScreenThread_->join();
      defaultScreenThread_ = nullptr;
    }
    playing_.clear();
  }

  // only call this in main thread
  bool setSize(const int width, const int height);

  ~VideoReader() {
    if (defaultScreenThread_) {
      quitDefaultScreen();
    }
    if (playbackThread_ || decodeThread_) {
      close();
    }
    av_frame_free(&curVideoFrame_);
    av_frame_free(&curAudioFrame_);
//    av_packet_free(&curVideoPacket_);
//    av_packet_free(&curAudioPacket_);
  }

  void decodeThreadBody();

  bool setupVideoCodecContext();

  bool setupAudioCodecContext();

  bool findAVStreams();


  // only call this in main thread
  bool handleEvent(const SDL_Event &event);


  bool tryReceiveAudioFrame();

  bool tryReceiveVideoFrame();


  bool resampleAudioFrame();

  bool rescaleVideoFrame();

  bool chooseFile();


  void setWindow(SDL_Window *);

  std::string to_string(const AVDictionary *dict) {
    std::string ret;
    AVDictionaryEntry *entry = new AVDictionaryEntry;
    while ((entry = av_dict_get(dict, "", entry, AV_DICT_IGNORE_SUFFIX))) {
      printf("Key: %s, Value: %s\n", entry->key, entry->value);
      ret += entry->key;
      ret += entry->value;
    }
    delete entry;
    return ret;
  }

 private:
  void resetSpeed();

};


#endif //ARCVEDITOR_SRC_VIDEO_READER_HH_
