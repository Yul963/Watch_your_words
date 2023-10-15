#ifndef WYW_SOURCE_DATA_H
#define WYW_SOURCE_DATA_H

#include <obs-module.h>
#include <obs.h>
#include <math.h>
#include <obs-frontend-api.h>
#include <vector>
#include <deque>
#include <queue>
#include <string>
#include <util/threading.h>

#include "whisper.h"

struct timestamp {
	uint64_t start;
	uint64_t end;
};

struct pair_audio {
	float **data;
	uint64_t timestamp;
};

struct wyw_source_data {
	obs_source_t *context;
	size_t channels;
	std::deque<struct pair_audio> audio_buf;
	//std::queue<struct obs_source_frame> video_buf;
	std::queue<struct timestamp> edit_timestamp;
	uint64_t start_timestamp;
	//struct obs_source_frame start_video_data;
	std::vector<std::string> words;
};

#endif