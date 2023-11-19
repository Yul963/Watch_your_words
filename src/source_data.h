#ifndef WYW_SOURCE_DATA_H
#define WYW_SOURCE_DATA_H

#include <obs-module.h>
#include <obs.h>
#include <math.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <vector>
#include <deque>
#include <queue>
#include <string>
#include <iostream>
#include <cctype>
#include <time.h>

#include <util/circlebuf.h>
#include <util/darray.h>
#include <media-io/audio-resampler.h>

#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <bitset>
#include <filesystem>

#include "whisper.h"
#include "rapidjson/document.h"

#define MT_ obs_module_text
#define DELAY_SEC 6

inline enum speaker_layout convert_speaker_layout(uint8_t channels)
{
	switch (channels) {
	case 0:
		return SPEAKERS_UNKNOWN;
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

struct edit_timestamp {
	std::string text;
	uint64_t start;
	uint64_t end;
	edit_timestamp(std::string text, uint64_t start, uint64_t end)
		: text(std::move(text)), start(start), end(end) 
	{}
};

enum DetectionResult {
	DETECTION_RESULT_UNKNOWN = 0,
	DETECTION_RESULT_SILENCE = 1,
	DETECTION_RESULT_SPEECH = 2,
};

struct DetectionResultWithText {
	DetectionResult result;
	std::string text;
	uint64_t start_timestamp_ms;
	uint64_t end_timestamp_ms;
	uint64_t start_timestamp;
};

struct pair_audio {
	float **data;
	uint64_t timestamp;
};

struct wyw_source_data {
	obs_source_t *context;
	size_t channels;
	size_t frames;

	uint32_t sample_rate;
	size_t overlap_frames;
	size_t overlap_ms;
	size_t last_num_frames;
	//size_t step_size_msec;
	uint64_t start_timestamp_ms;
	size_t sentence_number;

	float *copy_buffers[MAX_AUDIO_CHANNELS];
	struct circlebuf info_buffer;
	struct circlebuf input_buffers[MAX_AUDIO_CHANNELS];
	audio_resampler_t *resampler = nullptr;
	char *whisper_model_path = nullptr;
	struct whisper_context *whisper_context = nullptr;

	whisper_full_params whisper_params;
	float filler_p_threshold;
	bool do_silence;
	bool vad_enabled;
	bool active = false;
	bool save_srt = false;
	bool process_while_muted = false;
	bool use_text_source;
	bool censor;

	obs_weak_source_t *text_source = nullptr;
	char *text_source_name = nullptr;
	char *broadcast_type = nullptr;
	std::mutex *text_source_mutex = nullptr;
	std::queue<DetectionResultWithText> output;
	std::function<void(const DetectionResultWithText &result)> setTextCallback;
	std::string output_file_path = "";

	std::thread whisper_thread;
	std::mutex *whisper_buf_mutex = nullptr;
	std::mutex *whisper_ctx_mutex = nullptr;
	std::condition_variable *wshiper_thread_cv = nullptr;

	std::deque<struct pair_audio> audio_buf;
	//std::queue<struct obs_source_frame> video_buf;
	std::queue<struct edit_timestamp> timestamp_queue;
	std::vector<struct edit_timestamp> token_result;

	std::thread edit_thread;
	std::mutex *audio_buf_mutex = nullptr;
	std::mutex *timestamp_queue_mutex = nullptr;
	std::mutex *edit_mutex = nullptr;
	std::condition_variable *edit_thread_cv = nullptr;

	bool edit = false;

	char *edit_mode = nullptr;
	uint64_t start_timestamp;
	//struct obs_source_frame start_video_data;
	std::vector<std::string> banlist;

	std::vector<std::vector<std::string>> bantext;

	std::int16_t normalcnt;
	std::vector<std::int16_t> bancnt;
};

struct watch_your_words_audio_info {
	uint32_t frames;
	uint64_t timestamp;
};

void set_text_callback(struct wyw_source_data *gf, const DetectionResultWithText &str);

#endif