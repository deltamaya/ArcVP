//
// Created by delta on 1/22/2025.
//


#include "player.h"

std::tuple<int, int> findAVStream(AVFormatContext* formatContext){
	int videoStreamIndex = -1, audioStreamIndex = -1;
	videoStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	audioStreamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	return std::make_tuple(videoStreamIndex, audioStreamIndex);
}


bool Player::open(const char* filename){
	// open file and find stream info
	AVFormatContext* formatContext = nullptr;
	int ret = avformat_open_input(&formatContext, filename, nullptr, nullptr);
	if (ret != 0) {
		spdlog::error("Unable to open file '{}': {}", filename,av_err2str(ret));
		return false;
	}
	ret = avformat_find_stream_info(formatContext, nullptr);
	if (ret != 0) {
		spdlog::error("Unable to find stream info: {}",av_err2str(ret));
		return false;
	}

	// find video & audio stream
	auto [videoStreamIndex,audioStreamIndex] = findAVStream(formatContext);
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
	const AVCodecParameters *videoCodecParams = nullptr, *audioCodecParams = nullptr;
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
		if (ret = avcodec_parameters_to_context(videoCodecContext, videoCodecParams), ret < 0) {
			spdlog::error("Unable to initialize video codec context: {}", av_err2str(ret));
			return false;
		}
		if (ret = avcodec_open2(videoCodecContext, videoCodec, nullptr), ret < 0) {
			spdlog::error("Unable to open video codec: {}", av_err2str(ret));
			return false;
		}
		this->width=videoCodecParams->width;
		this->height=videoCodecParams->height;
	}

	if (hasAudio) {
		audioCodecContext = avcodec_alloc_context3(audioCodec);
		if (!audioCodecContext) {
			spdlog::error("Unable to allocate audio codec context");
			std::exit(1);
		}
		if (ret = avcodec_parameters_to_context(audioCodecContext, audioCodecParams), ret < 0) {
			spdlog::error("Unable to initialize audio codec context: {}", av_err2str(ret));
			return false;
		}
		if (ret = avcodec_open2(audioCodecContext, audioCodec, nullptr), ret < 0) {
			spdlog::error("Unable to open audio codec: {}", av_err2str(ret));
			return false;
		}
	}

	this->formatContext = formatContext;

	this->videoStream = videoStream;
	this->videoCodec = videoCodec;
	this->videoCodecParams = videoCodecParams;
	this->videoStreamIndex = videoStreamIndex;
	this->videoCodecContext = videoCodecContext;

	this->audioStream = audioStream;
	this->audioCodec = audioCodec;
	this->audioCodecParams = audioCodecParams;
	this->audioStreamIndex = audioStreamIndex;
	this->audioCodecContext = audioCodecContext;
	spdlog::info("Opened file '{}'", filename);
	return true;
}

void Player::close(){
	this->formatContext = nullptr;

	this->videoStream = nullptr;
	this->videoCodec = nullptr;
	this->videoCodecParams = nullptr;
	this->videoStreamIndex = -1;
	this->videoCodecContext = nullptr;

	this->audioStream = nullptr;
	this->audioCodec = nullptr;
	this->audioCodecParams = nullptr;
	this->audioStreamIndex = -1;
	this->audioCodecContext = nullptr;
	spdlog::info("Closed Input");
}