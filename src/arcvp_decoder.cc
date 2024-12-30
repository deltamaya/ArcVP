//
// Created by delta on 12/30/2024.
//


#include "arcvp.h"

#include <iostream>

using std::cout;

void ArcVP::decoderFunc() {
  AVPacket*packet=av_packet_alloc();
  while(true) {
    int ret=av_read_frame(formatContext,packet);
    if(ret!=0) {
      if (ret == AVERROR_EOF) {
        cout << "Decoder reached EOF\n";
        break;
      }
      cout << "Unexpected error in decoder: " << av_err2str(ret) << "\n";
      break;
    }
    if(packet->stream_index==videoStream->index) {
      videoPacketQueue.push(packet);
    }else if(packet->stream_index==audioStream->index) {
      audioPacketQueue.push(packet);
    }
    av_packet_unref(packet);

  }
}
