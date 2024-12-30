//
// Created by maya delta on 2024/12/30.
//

#include "arcvp.h"

#include <iostream>

extern "C"{
#include <libavutil/imgutils.h>
}
using std::cerr;
// Function to convert YUV AVFrame to RGB buffer
AVFrame* ConvertYUVToRGB(AVFrame* yuvFrame, int width, int height) {
  // Step 1: Allocate RGB buffer
  int rgbBufferSize =av_image_get_buffer_size(AV_PIX_FMT_RGB24,width,height,1);
  unsigned char* buffer = (unsigned char*)av_malloc(rgbBufferSize);
  if (!buffer) {
    std::cerr << "Failed to allocate RGB buffer." << std::endl;
    return nullptr;
  }
  AVFrame* rgbFrame = av_frame_alloc();
  if (!rgbFrame) {
    std::cerr << "Failed to allocate RGB frame." << std::endl;
    return nullptr;
  }
  av_image_fill_arrays(rgbFrame->data, rgbFrame->linesize, buffer, AV_PIX_FMT_RGB24, width, height, 1);

  // Step 3: Initialize SwsContext for YUV to RGB conversion
  SwsContext* swsCtx = sws_getContext(
      yuvFrame->width, yuvFrame->height, (AVPixelFormat)yuvFrame->format,  // Source format and dimensions
      width, height, AV_PIX_FMT_RGB24,                // Destination format and dimensions
      SWS_BILINEAR,                                   // Scaling algorithm
      nullptr, nullptr, nullptr
  );
  if (!swsCtx) {
    std::cerr << "Failed to create SwsContext." << std::endl;
    return nullptr;
  }
  // Step 4: Perform the conversion
  sws_scale(
      swsCtx,
      yuvFrame->data, yuvFrame->linesize,  // Source data and line sizes
      0, height,                          // Source slice (start and height)
      rgbFrame->data, rgbFrame->linesize  // Destination data and line sizes
  );

  // Step 5: Cleanup
  sws_freeContext(swsCtx);

  // Return the RGB buffer
  return rgbFrame;
}

void ArcVP::playerFunc() {

}

std::optional<AVFrame*> ArcVP::tryReceiveVideoFrame() {
  AVFrame*frame=nullptr;
  int ret=avcodec_receive_frame(videoCodecContext,frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return std::nullopt;
  }
  return frame;
}


std::optional<AVFrame*> ArcVP::tryReceiveAudioFrame() {
  AVFrame*frame=nullptr;
  int ret=avcodec_receive_frame(audioCodecContext,frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    return std::nullopt;
  }
  return frame;
}