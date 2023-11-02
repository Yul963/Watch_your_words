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
#include <iostream>
#include <cctype>

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

#define MT_ obs_module_text

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
	size_t step_size_msec;
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
	bool caption_to_stream;
	bool active = false;
	bool save_srt = false;
	bool process_while_muted = false;
	bool rename_file_to_match_recording = false;

	obs_weak_source_t *text_source = nullptr;
	char *text_source_name = nullptr;
	std::mutex *text_source_mutex = nullptr;

	std::function<void(const DetectionResultWithText &result)> setTextCallback;
	std::string output_file_path = "";
	std::string whisper_model_file_currently_loaded = "";

	std::thread whisper_thread;
	std::mutex *whisper_buf_mutex = nullptr;
	std::mutex *whisper_ctx_mutex = nullptr;
	std::condition_variable *wshiper_thread_cv = nullptr;

	std::thread edit_thread;
	std::deque<struct pair_audio> audio_buf;
	//std::queue<struct obs_source_frame> video_buf;
	std::queue<struct edit_timestamp> timestamp_queue;
	std::vector<struct edit_timestamp> token_result;
	std::mutex *audio_buf_mutex = nullptr;
	std::mutex *timestamp_queue_mutex = nullptr;
	uint64_t start_timestamp;
	uint64_t current_timestamp;
	//struct obs_source_frame start_video_data;
	std::vector<std::string> banlist;
};

struct watch_your_words_audio_info {
	uint32_t frames;
	uint64_t timestamp;
};

void set_text_callback(struct wyw_source_data *gf, const DetectionResultWithText &str);

#endif