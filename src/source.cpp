﻿#include "source.h"
#include "source_data.h"
#include <plugin-support.h>

#include "whisper-processing.h"
#include "whisper-language.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define do_log(level, format, ...)                 \
blog(level, "[word filter: '%s'] " format, \
	     obs_source_get_name(wf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

bool started;

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

inline uint64_t now_ms()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool add_sources_to_list(void *list_property, obs_source_t *source)//자막 출력 추가
{
	auto source_id = obs_source_get_id(source);
	if (strcmp(source_id, "text_ft2_source_v2") != 0 && strcmp(source_id, "text_gdiplus_v2") != 0) {
		return true;
	}

	obs_property_t *sources = (obs_property_t *)list_property;
	const char *name = obs_source_get_name(source);
	obs_property_list_add_string(sources, name, name);
	return true;
}

 void copy_obs_audio_data(float **dest, float **src, size_t channels)
{
	for (int i = 0; i < channels; i++) {
		memcpy(dest[i], src[i], sizeof(float) * 480);
	}
}

double sin_val = 0.0;
struct obs_audio_data *wyw_source_filter_audio(void *data, struct obs_audio_data *audio)
{
	double rate = 1000.0 / 48000.0;
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	const size_t channels = wf->channels;
	float **fdata = (float **)audio->data;
	if (!started) {
		started = true;
		wf->start_timestamp = audio->timestamp;
	}
	float **a = new float *[channels];
	for (int i = 0; i < channels; i++) {
		a[i] = new float[480];
	}
	copy_obs_audio_data(a, fdata, channels);
	struct pair_audio b = {a, audio->timestamp};
	wf->audio_buf.push_back(b);
	//obs_log(LOG_INFO, "start timstamp: %llu)",wf->start_timestamp);
	if (audio->timestamp <= (wf->start_timestamp + 5000000000)) {
		for (size_t c = 0; c < channels; c++) {
			if (audio->data[c]) {
				for (size_t i = 0; i < audio->frames; i++) {
					fdata[c][i] = 0.f;
				}
			}
		}
	} else {
		if (!wf->audio_buf.empty()) {
			struct pair_audio temp = wf->audio_buf.front();
			copy_obs_audio_data(fdata, temp.data, channels);
			//obs_log(LOG_INFO, "timstamp2: %llu)", audio->timestamp);
			wf->audio_buf.pop_front();
			for (int i = 0; i < channels; i++) {
				delete temp.data[i];
			}
			delete temp.data;
		}
	}

	if (!wf->edit_timestamp.empty()) {
		struct timestamp temp_t = wf->edit_timestamp.front();
		wf->edit_timestamp.pop();
		std::deque<struct pair_audio> temp_q = wf->audio_buf;
		for (int i = 0; i < temp_q.size(); i++) {
			if ((temp_q[i].timestamp > temp_t.start) &&
			    (temp_q[i].timestamp < temp_t.end)) {
				for (size_t c = 0; c < channels; c++) {
					if (audio->data[c]) {
						for (size_t j = 0;
						     j < audio->frames; i++) {
							sin_val += rate * M_PI *
								   2.0;
							if (sin_val >
							    M_PI * 2.0)
								sin_val -=
									M_PI *
									2.0;
							double wave =
								sin(sin_val);
							temp_q[i].data[c][j] =
								(float)(wave /
									100.0);
						}
					}
				}
			}
			if (temp_q[i].timestamp > temp_t.end)
				break;
		}
	}
	/* temp = adata[c][i];
	adata[c][i] = adata[c][audio->frames - i - 1];
	adata[c][audio->frames-i] = temp;*/

	if (!wf->active) {
		return audio;
	}
	obs_source_t *parent_source = obs_filter_get_parent(wf->context);
	if (wf->process_while_muted == false && obs_source_muted(parent_source)) {
		return audio;
	}
	if (wf->whisper_context == nullptr) {
		return audio;
	}
	if (!wf->whisper_buf_mutex || !wf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR, "whisper mutexes are null");
		return audio;
	}

	{
		std::lock_guard<std::mutex> lock(*wf->whisper_buf_mutex);
		for (size_t c = 0; c < wf->channels; c++)
			circlebuf_push_back(&wf->input_buffers[c], wf->audio_buf.back().data[c], audio->frames * sizeof(float));
		struct watch_your_words_audio_info info = {0};
		info.frames = audio->frames;
		info.timestamp = audio->timestamp;
		circlebuf_push_back(&wf->info_buffer, &info, sizeof(info));
	}

	return audio;
}

const char *wyw_source_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("WatchYourWord");
}

void wyw_source_destroy(void *data)
{
	struct wyw_source_data *wf = (struct wyw_source_data *)data;

	{
		std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
		if (wf->whisper_context != nullptr) {
			whisper_free(wf->whisper_context);
			wf->whisper_context = nullptr;
			wf->wshiper_thread_cv->notify_all();
		}
	}

	if (wf->whisper_thread.joinable()) {
		wf->whisper_thread.join();
	}

	if (wf->text_source_name) {
		bfree(wf->text_source_name);
		wf->text_source_name = nullptr;
	}

	if (wf->text_source) {
		obs_weak_source_release(wf->text_source);
		wf->text_source = nullptr;
	}

	if (wf->resampler) {
		audio_resampler_destroy(wf->resampler);
	}

	{
		std::lock_guard<std::mutex> lockbuf(*wf->whisper_buf_mutex);
		bfree(wf->copy_buffers[0]);
		wf->copy_buffers[0] = nullptr;
		for (size_t i = 0; i < wf->channels; i++) {
			circlebuf_free(&wf->input_buffers[i]);
		}
	}
	circlebuf_free(&wf->info_buffer);

	delete wf->whisper_buf_mutex;
	delete wf->whisper_ctx_mutex;
	delete wf->wshiper_thread_cv;
	delete wf->text_source_mutex;

	for (; !wf->audio_buf.empty();) {
		//obs_log(LOG_INFO, "audio_buf emptying.");
		struct pair_audio temp = wf->audio_buf.front();
		for (int i = 0; i < wf->channels; i++) {
			delete temp.data[i];
		}
		delete temp.data;
		wf->audio_buf.pop_front();
	}
	
	bfree(wf);
	obs_log(LOG_INFO,"watch-your-words source destroyed.");
}

void acquire_weak_text_source_ref(struct wyw_source_data *wf)
{
	if (!wf->text_source_name) {
		obs_log(LOG_ERROR, "text_source_name is null");
		return;
	}

	std::lock_guard<std::mutex> lock(*wf->text_source_mutex);

	// acquire a weak ref to the new text source
	obs_source_t *source = obs_get_source_by_name(wf->text_source_name);
	if (source) {
		wf->text_source = obs_source_get_weak_source(source);
		obs_source_release(source);
		if (!wf->text_source) {
			obs_log(LOG_ERROR,
				"failed to get weak source for text source %s",
				wf->text_source_name);
		}
	} else {
		obs_log(LOG_ERROR, "text source '%s' not found",
			wf->text_source_name);
	}
}

void set_text_callback(struct wyw_source_data *wf, const DetectionResultWithText &result)
{
#ifdef _WIN32
	std::string str_copy = result.text;
	for (size_t i = 0; i < str_copy.size(); ++i) {
		if ((str_copy.c_str()[i] & 0xf0) == 0xf0) {
			str_copy[i] = (str_copy.c_str()[i] & 0x0f) | 0xd0;
		}
	}
#else
	std::string str_copy = result.text;
#endif
	if (wf->caption_to_stream) {
		obs_output_t *streaming_output = obs_frontend_get_streaming_output();
		if (streaming_output) {
			obs_output_output_caption_text1(streaming_output, str_copy.c_str());
			obs_output_release(streaming_output);
		}
	}

	if (wf->output_file_path != "" && !wf->text_source_name) {
		// Check if we should save the sentence
		if (wf->save_only_while_recording &&
		    !obs_frontend_recording_active()) {
			// We are not recording, do not save the sentence to file
			return;
		}
		if (!wf->save_srt) {
			// Write raw sentence to file, do not append
			std::ofstream output_file(wf->output_file_path, std::ios::out | std::ios::trunc);
			output_file << str_copy << std::endl;
			output_file.close();
		} else {
			obs_log(LOG_INFO, "Saving sentence to file %s, sentence #%d", wf->output_file_path.c_str(), wf->sentence_number);
			// Append sentence to file in .srt format
			std::ofstream output_file(wf->output_file_path, std::ios::out | std::ios::app);
			output_file << wf->sentence_number << std::endl;
			// use the start and end timestamps to calculate the start and end time in srt format
			auto format_ts_for_srt = [&output_file](uint64_t ts) {
				uint64_t time_s = ts / 1000;
				uint64_t time_m = time_s / 60;
				uint64_t time_h = time_m / 60;
				uint64_t time_ms_rem = ts % 1000;
				uint64_t time_s_rem = time_s % 60;
				uint64_t time_m_rem = time_m % 60;
				uint64_t time_h_rem = time_h % 60;
				output_file << std::setfill('0') << std::setw(2)
					    << time_h_rem << ":"
					    << std::setfill('0') << std::setw(2)
					    << time_m_rem << ":"
					    << std::setfill('0') << std::setw(2)
					    << time_s_rem << ","
					    << std::setfill('0') << std::setw(3)
					    << time_ms_rem;
			};
			format_ts_for_srt(result.start_timestamp_ms);
			output_file << " --> ";
			format_ts_for_srt(result.end_timestamp_ms);
			output_file << std::endl;

			output_file << str_copy << std::endl;
			output_file << std::endl;
			output_file.close();
			wf->sentence_number++;
		}
	} else {
		if (!wf->text_source_mutex) {
			obs_log(LOG_ERROR, "text_source_mutex is null");
			return;
		}

		if (!wf->text_source) {
			// attempt to acquire a weak ref to the text source if it's yet available
			acquire_weak_text_source_ref(wf);
		}

		std::lock_guard<std::mutex> lock(*wf->text_source_mutex);

		if (!wf->text_source) {
			obs_log(LOG_ERROR, "text_source is null");
			return;
		}
		auto target = obs_weak_source_get_source(wf->text_source);
		if (!target) {
			obs_log(LOG_ERROR, "text_source target is null");
			return;
		}
		auto text_settings = obs_source_get_settings(target);
		obs_data_set_string(text_settings, "text", str_copy.c_str());
		obs_source_update(target, text_settings);
		obs_source_release(target);
	}
};

void shutdown_whisper_thread(struct wyw_source_data *wf)
{
	obs_log(LOG_INFO, "shutdown_whisper_thread");
	if (wf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		if (!wf->whisper_ctx_mutex || !wf->wshiper_thread_cv) {
			obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
			return;
		}
		std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
		whisper_free(wf->whisper_context);
		wf->whisper_context = nullptr;
		wf->wshiper_thread_cv->notify_all();
	}
	if (wf->whisper_thread.joinable()) {
		wf->whisper_thread.join();
	}
	if (wf->whisper_model_path != nullptr) {
		bfree(wf->whisper_model_path);
		wf->whisper_model_path = nullptr;
	}
}

void start_whisper_thread_with_path(struct wyw_source_data *wf, const std::string &path)
{
	obs_log(LOG_INFO, "start_whisper_thread_with_path: %s", path.c_str());
	if (!wf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_ctx_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
	if (wf->whisper_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init whisper: whisper_context is not null");
		return;
	}
	wf->whisper_context = init_whisper_context(path);
	wf->whisper_model_file_currently_loaded = path;
	std::thread new_whisper_thread(whisper_loop, wf);
	wf->whisper_thread.swap(new_whisper_thread);
}

void wyw_source_update(void *data, obs_data_t *s)
{
	//obs_log(LOG_INFO, "watch-your-words source updating.");
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	obs_data_array_t *data_array = obs_data_get_array(s, "ban list");
	wf->words.clear();
	for (int i = 0; i < obs_data_array_count(data_array); i++) {
		obs_data_t *string = obs_data_array_item(data_array, i);
		const char *a = obs_data_get_string(string, "value");
		wf->words.push_back(std::string(a));
	}
	wf->channels = audio_output_get_channels(obs_get_audio());

	wf->vad_enabled = obs_data_get_bool(s, "vad_enabled");
	wf->caption_to_stream = obs_data_get_bool(s, "caption_to_stream");
	bool step_by_step_processing = obs_data_get_bool(s, "step_by_step_processing");
	wf->step_size_msec = step_by_step_processing ? (int)obs_data_get_int(s, "step_size_msec") : BUFFER_SIZE_MSEC;
	wf->save_srt = obs_data_get_bool(s, "subtitle_save_srt");
	wf->save_only_while_recording = obs_data_get_bool(s, "only_while_recording");
	wf->rename_file_to_match_recording = obs_data_get_bool(s, "rename_file_to_match_recording");
	// Get the current timestamp using the system clock
	wf->start_timestamp_ms = now_ms();
	wf->sentence_number = 1;
	wf->process_while_muted = obs_data_get_bool(s, "process_while_muted");

	const char *new_text_source_name = obs_data_get_string(s, "subtitle_sources");
	obs_weak_source_t *old_weak_text_source = NULL;

	if (new_text_source_name == nullptr ||
	    strcmp(new_text_source_name, "none") == 0 ||
	    strcmp(new_text_source_name, "(null)") == 0 ||
	    strcmp(new_text_source_name, "text_file") == 0 ||
	    strlen(new_text_source_name) == 0) {
		// new selected text source is not valid, release the old one
		if (wf->text_source) {
			if (!wf->text_source_mutex) {
				obs_log(LOG_ERROR, "text_source_mutex is null");
				return;
			}
			std::lock_guard<std::mutex> lock(*wf->text_source_mutex);
			old_weak_text_source = wf->text_source;
			wf->text_source = nullptr;
		}
		if (wf->text_source_name) {
			bfree(wf->text_source_name);
			wf->text_source_name = nullptr;
		}
		wf->output_file_path = "";
		if (strcmp(new_text_source_name, "text_file") == 0) {
			// set the output file path
			const char *output_file_path = obs_data_get_string(s, "subtitle_output_filename");
			if (output_file_path != nullptr && strlen(output_file_path) > 0) {
				wf->output_file_path = output_file_path;
			}
		}
	} else {
		// new selected text source is valid, check if it's different from the old one
		if (wf->text_source_name == nullptr ||
		    strcmp(new_text_source_name, wf->text_source_name) != 0) {
			// new text source is different from the old one, release the old one
			if (wf->text_source) {
				if (!wf->text_source_mutex) {
					obs_log(LOG_ERROR,"text_source_mutex is null");
					return;
				}
				std::lock_guard<std::mutex> lock(*wf->text_source_mutex);
				old_weak_text_source = wf->text_source;
				wf->text_source = nullptr;
			}
			if (wf->text_source_name) {
				// free the old text source name
				bfree(wf->text_source_name);
				wf->text_source_name = nullptr;
			}
			wf->text_source_name = bstrdup(new_text_source_name);
		}
	}

	if (old_weak_text_source) {
		obs_log(LOG_INFO, "releasing old text source");
		obs_weak_source_release(old_weak_text_source);
	}

	obs_log(LOG_INFO, "watch_your_words: update whisper model");
	// update the whisper model path
	std::string new_model_path = obs_data_get_string(s, "whisper_model_path");

	if (wf->whisper_model_path == nullptr ||
	    strcmp(new_model_path.c_str(), wf->whisper_model_path) != 0) {
		// model path changed, reload the model
		obs_log(LOG_INFO, "model path changed from %s to %s", wf->whisper_model_path, new_model_path.c_str());

		shutdown_whisper_thread(wf);
		wf->whisper_model_path = bstrdup(new_model_path.c_str());

		obs_log(LOG_INFO, "Checking if model %s exists in data...", wf->whisper_model_path);
		char *model_config_path_str = obs_module_get_config_path(obs_current_module(), wf->whisper_model_path);
		std::string model_config_path(model_config_path_str);
		bfree(model_config_path_str);
		obs_log(LOG_INFO, "Model path in config: %s", model_config_path.c_str());

		if (std::filesystem::exists(model_config_path)) {
			obs_log(LOG_INFO, "Model exists in config folder: %s", model_config_path.c_str());
		}

		if (model_config_path == "") {
			obs_log(LOG_ERROR, "Whisper model does not exist");
			/* download_model_with_ui_dialog(
				wf->whisper_model_path,
				[wf](int download_status,
					    const std::string &path) {
					if (download_status == 0) {
						obs_log(LOG_INFO,
							"Model download complete");
						start_whisper_thread_with_path(
							gf, path);
					} else {
						obs_log(LOG_ERROR,
							"Model download failed");
					}
				});*/
		} else {
			start_whisper_thread_with_path(wf, model_config_path);
		}
	} else {
		obs_log(LOG_INFO, "model path did not change: %s == %s",
			wf->whisper_model_path, new_model_path.c_str());
	}

	if (!wf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
		return;
	}
	obs_log(LOG_INFO, "watch_your_words: update whisper params");
	std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);

	wf->whisper_params = whisper_full_default_params((whisper_sampling_strategy)obs_data_get_int(s, "whisper_sampling_method"));
	wf->whisper_params.duration_ms = BUFFER_SIZE_MSEC;
	wf->whisper_params.language = obs_data_get_string(s, "whisper_language_select");
	wf->whisper_params.initial_prompt = obs_data_get_string(s, "initial_prompt");
	wf->whisper_params.n_threads = (int)obs_data_get_int(s, "n_threads");
	wf->whisper_params.n_max_text_ctx = (int)obs_data_get_int(s, "n_max_text_ctx");
	wf->whisper_params.translate = obs_data_get_bool(s, "translate");
	wf->whisper_params.no_context = obs_data_get_bool(s, "no_context");
	wf->whisper_params.single_segment = obs_data_get_bool(s, "single_segment");
	wf->whisper_params.print_special = obs_data_get_bool(s, "print_special");
	wf->whisper_params.print_progress = obs_data_get_bool(s, "print_progress");
	wf->whisper_params.print_realtime = obs_data_get_bool(s, "print_realtime");
	wf->whisper_params.print_timestamps = obs_data_get_bool(s, "print_timestamps");
	wf->whisper_params.token_timestamps = obs_data_get_bool(s, "token_timestamps");
	wf->whisper_params.thold_pt = (float)obs_data_get_double(s, "thold_pt");
	wf->whisper_params.thold_ptsum = (float)obs_data_get_double(s, "thold_ptsum");
	wf->whisper_params.max_len = (int)obs_data_get_int(s, "max_len");
	wf->whisper_params.split_on_word = obs_data_get_bool(s, "split_on_word");
	wf->whisper_params.max_tokens = (int)obs_data_get_int(s, "max_tokens");
	wf->whisper_params.speed_up = obs_data_get_bool(s, "speed_up");
	wf->whisper_params.suppress_blank = obs_data_get_bool(s, "suppress_blank");
	wf->whisper_params.suppress_non_speech_tokens = obs_data_get_bool(s, "suppress_non_speech_tokens");
	wf->whisper_params.temperature = (float)obs_data_get_double(s, "temperature");
	wf->whisper_params.max_initial_ts = (float)obs_data_get_double(s, "max_initial_ts");
	wf->whisper_params.length_penalty = (float)obs_data_get_double(s, "length_penalty");
}

void *wyw_source_create(obs_data_t *settings, obs_source_t *filter)
{
	char *module_config_path = obs_module_get_config_path(obs_current_module(), "models");
	obs_log(LOG_INFO, "module_config_path: %s)", module_config_path);
	started = false;
	struct wyw_source_data *wf = new wyw_source_data();
	wf->context = filter;

	wf->channels = audio_output_get_channels(obs_get_audio());
	wf->sample_rate = audio_output_get_sample_rate(obs_get_audio());
	wf->frames = (size_t)((float)wf->sample_rate /(1000.0f / (float)BUFFER_SIZE_MSEC));
	wf->last_num_frames = 0;
	bool step_by_step_processing = obs_data_get_bool(settings, "step_by_step_processing");
	wf->step_size_msec = step_by_step_processing ? (int)obs_data_get_int(settings, "step_size_msec") : BUFFER_SIZE_MSEC;
	wf->save_srt = obs_data_get_bool(settings, "subtitle_save_srt");
	wf->save_only_while_recording = obs_data_get_bool(settings, "only_while_recording");
	wf->rename_file_to_match_recording = obs_data_get_bool(settings, "rename_file_to_match_recording");
	wf->process_while_muted = obs_data_get_bool(settings, "process_while_muted");

	for (size_t i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		circlebuf_init(&wf->input_buffers[i]);
	}
	circlebuf_init(&wf->info_buffer);

	// allocate copy buffers
	wf->copy_buffers[0] = static_cast<float *>(bzalloc(wf->channels * wf->frames * sizeof(float)));
	for (size_t c = 1; c < wf->channels; c++) { // set the channel pointers
		wf->copy_buffers[c] = wf->copy_buffers[0] + c * wf->frames;
	}

	wf->context = filter;
	wf->overlap_ms = OVERLAP_SIZE_MSEC;
	wf->overlap_frames = (size_t)((float)wf->sample_rate / (1000.0f / (float)wf->overlap_ms));
	obs_log(LOG_INFO, "watch_your_words: channels %d, frames %d, sample_rate %d",(int)wf->channels, (int)wf->frames, wf->sample_rate);
	obs_log(LOG_INFO, "watch_your_words: setup audio resampler");
	struct resample_info src, dst;
	src.samples_per_sec = wf->sample_rate;
	src.format = AUDIO_FORMAT_FLOAT_PLANAR;
	src.speakers = convert_speaker_layout((uint8_t)wf->channels);

	dst.samples_per_sec = WHISPER_SAMPLE_RATE;
	dst.format = AUDIO_FORMAT_FLOAT_PLANAR;
	dst.speakers = convert_speaker_layout((uint8_t)1);
	wf->resampler = audio_resampler_create(&dst, &src);

	obs_log(LOG_INFO, "watch_your_words: setup mutexes and condition variables");
	wf->whisper_buf_mutex = new std::mutex();
	wf->whisper_ctx_mutex = new std::mutex();
	wf->wshiper_thread_cv = new std::condition_variable();
	wf->text_source_mutex = new std::mutex();
	obs_log(LOG_INFO, "watch_your_words: clear text source data");
	wf->text_source = nullptr;
	const char *subtitle_sources = obs_data_get_string(settings, "subtitle_sources");
	if (subtitle_sources != nullptr) {
		wf->text_source_name = bstrdup(subtitle_sources);
	} else {
		wf->text_source_name = nullptr;
	}
	obs_log(LOG_INFO, "watch_your_words: clear paths and whisper context");
	wf->whisper_model_file_currently_loaded = "";
	wf->output_file_path = std::string("");
	wf->whisper_model_path = nullptr;
	wf->whisper_context = nullptr;

	wyw_source_update(wf, settings);

	wf->active = true;

	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *private_data) {
			if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING) {
				struct wyw_source_data *wf_ = static_cast<struct wyw_source_data*>(private_data);
				if (wf_->save_srt &&
				    wf_->save_only_while_recording) {
					obs_log(LOG_INFO,"Recording started. Resetting srt file.");
					std::ofstream output_file(wf_->output_file_path, std::ios::out | std::ios::trunc);
					output_file.close();
					wf_->sentence_number = 1;
					wf_->start_timestamp_ms = now_ms();
				}
			} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
				struct wyw_source_data *wf_ = static_cast<struct wyw_source_data*>(private_data);
				if (wf_->save_srt && wf_->save_only_while_recording && wf_->rename_file_to_match_recording) {
					obs_log(LOG_INFO, "Recording stopped. Rename srt file.");
					std::string recording_file_name = obs_frontend_get_last_recording();
					recording_file_name = recording_file_name.substr(0,recording_file_name.find_last_of("."));
					std::string srt_file_name = recording_file_name + ".srt";
					std::rename(wf_->output_file_path.c_str(), srt_file_name.c_str());
				}}},wf);
	//obs_log(LOG_INFO, "watch-your-words source created.");
	return wf;
}

void checkBanList() {
	
}

void setTimestamp() {

}

struct obs_source_frame *wyw_source_filter_video(void *data, struct obs_source_frame *frame)
{
	return frame;
}

void wyw_source_defaults(obs_data_t *s) {
	obs_log(LOG_INFO, "watch_your_words_defaults");

	obs_data_set_default_bool(s, "vad_enabled", true);
	obs_data_set_default_bool(s, "caption_to_stream", false);
	obs_data_set_default_string(s, "whisper_model_path", "models/ggml-large.bin");
	obs_data_set_default_string(s, "whisper_language_select", "ko");
	obs_data_set_default_string(s, "subtitle_sources", "none");
	obs_data_set_default_bool(s, "step_by_step_processing", false);
	obs_data_set_default_bool(s, "process_while_muted", false);
	obs_data_set_default_bool(s, "subtitle_save_srt", false);
	obs_data_set_default_bool(s, "only_while_recording", false);
	obs_data_set_default_bool(s, "rename_file_to_match_recording", true);
	obs_data_set_default_int(s, "step_size_msec", 1000);

	// Whisper parameters
	obs_data_set_default_int(s, "whisper_sampling_method", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_string(s, "initial_prompt", "");
	obs_data_set_default_int(s, "n_threads", 4);
	obs_data_set_default_int(s, "n_max_text_ctx", 16384);
	obs_data_set_default_bool(s, "translate", false);
	obs_data_set_default_bool(s, "no_context", true);
	obs_data_set_default_bool(s, "single_segment", true);
	obs_data_set_default_bool(s, "print_special", false);
	obs_data_set_default_bool(s, "print_progress", false);
	obs_data_set_default_bool(s, "print_realtime", false);
	obs_data_set_default_bool(s, "print_timestamps", false);
	obs_data_set_default_bool(s, "token_timestamps", false);
	obs_data_set_default_double(s, "thold_pt", 0.01);
	obs_data_set_default_double(s, "thold_ptsum", 0.01);
	obs_data_set_default_int(s, "max_len", 0);
	obs_data_set_default_bool(s, "split_on_word", true);
	obs_data_set_default_int(s, "max_tokens", 32);
	obs_data_set_default_bool(s, "speed_up", false);
	obs_data_set_default_bool(s, "suppress_blank", false);
	obs_data_set_default_bool(s, "suppress_non_speech_tokens", true);
	obs_data_set_default_double(s, "temperature", 0.5);
	obs_data_set_default_double(s, "max_initial_ts", 1.0);
	obs_data_set_default_double(s, "length_penalty", -1.0);
}

obs_properties_t *wyw_source_properties(void *data)
{
	struct wyw_source_data *wf = static_cast<struct wyw_source_data *>(data);
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_editable_list(ppts,"ban list",MT_("ban list"),OBS_EDITABLE_LIST_TYPE_STRINGS,NULL, NULL);

	obs_properties_add_bool(ppts, "vad_enabled", MT_("vad_enabled"));
	obs_properties_add_bool(ppts, "log_words", MT_("log_words"));
	obs_properties_add_bool(ppts, "caption_to_stream", MT_("caption_to_stream"));
	obs_property_t *step_by_step_processing = obs_properties_add_bool(ppts, "step_by_step_processing", MT_("step_by_step_processing"));
	obs_properties_add_int_slider(ppts, "step_size_msec", MT_("step_size_msec"), 1000, BUFFER_SIZE_MSEC, 50);

	obs_property_set_modified_callback(
		step_by_step_processing,
		[](obs_properties_t *props, obs_property_t *property,
		   obs_data_t *settings) {
			UNUSED_PARAMETER(property);
			// Show/Hide the step size input
			obs_property_set_visible(
				obs_properties_get(props, "step_size_msec"),
				obs_data_get_bool(settings,
						  "step_by_step_processing"));
			return true;
		});

	obs_properties_add_bool(ppts, "process_while_muted", MT_("process_while_muted"));
	obs_property_t *subs_output = obs_properties_add_list(
		ppts, "subtitle_sources", MT_("subtitle_sources"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(subs_output, MT_("none_no_output"),
				     "none");
	obs_property_list_add_string(subs_output, MT_("text_file_output"),
				     "text_file");
	// Add text sources
	obs_enum_sources(add_sources_to_list, subs_output);

	obs_properties_add_path(ppts, "subtitle_output_filename", MT_("output_filename"), OBS_PATH_FILE_SAVE, "Text (*.txt)", NULL);
	obs_properties_add_bool(ppts, "subtitle_save_srt", MT_("save_srt"));
	obs_properties_add_bool(ppts, "only_while_recording", MT_("only_while_recording"));
	obs_properties_add_bool(ppts, "rename_file_to_match_recording", MT_("rename_file_to_match_recording"));

	obs_property_set_modified_callback(
		subs_output,[](obs_properties_t *props, obs_property_t *property,obs_data_t *settings) {
			UNUSED_PARAMETER(property);
			// Show or hide the output filename selection input
			const char *new_output = obs_data_get_string(settings, "subtitle_sources");
			const bool show_hide =(strcmp(new_output, "text_file") == 0);
			obs_property_set_visible(obs_properties_get(props,"subtitle_output_filename"),show_hide);
			obs_property_set_visible(obs_properties_get(props, "subtitle_save_srt"),show_hide);
			obs_property_set_visible(obs_properties_get(props,"only_while_recording"),show_hide);
			obs_property_set_visible(obs_properties_get(props,"rename_file_to_match_recording"),show_hide);
			return true;
		});

	// Add a list of available whisper models to download
	obs_property_t *whisper_models_list = obs_properties_add_list(ppts, "whisper_model_path", MT_("whisper_model"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(whisper_models_list, "Tiny 75Mb","models/ggml-tiny.bin");
	obs_property_list_add_string(whisper_models_list, "Base 142Mb","models/ggml-base.bin");
	obs_property_list_add_string(whisper_models_list, "Small 466Mb","models/ggml-small.bin");
	obs_property_list_add_string(whisper_models_list, "Medium 1.42Gb","models/ggml-medium.bin");
	obs_property_list_add_string(whisper_models_list, "Large 2.88Gb","models/ggml-large.bin");
	obs_property_list_add_string(whisper_models_list, "Tiny Quantized 30Mb","models/ggml-tiny-q5_1.bin");
	obs_property_list_add_string(whisper_models_list, "Base Quantized 57Mb","models/ggml-base-q5_1.bin");
	obs_property_list_add_string(whisper_models_list, "Small Quantized 181Mb","models/ggml-small-q5_1.bin");
	obs_property_list_add_string(whisper_models_list, "Medium Quantized 514Mb","models/ggml-medium-q5_0.bin");
	obs_property_list_add_string(whisper_models_list, "Large Quantized 1Gb","models/ggml-large-q5_0.bin");

	obs_properties_t *whisper_params_group = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", MT_("whisper_parameters"), OBS_GROUP_NORMAL, whisper_params_group);

	obs_property_t *whisper_language_select_list = obs_properties_add_list(whisper_params_group, "whisper_language_select",
		MT_("language"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	std::map<std::string, std::string> whisper_available_lang_flip;
	for (auto const &pair : whisper_available_lang) {
		whisper_available_lang_flip[pair.second] = pair.first;
	}

	for (auto const &pair : whisper_available_lang_flip) {
		std::string language_name = pair.first;
		language_name[0] = (char)toupper(language_name[0]);

		obs_property_list_add_string(whisper_language_select_list, language_name.c_str(), pair.second.c_str());
	}

	obs_property_t *whisper_sampling_method_list = obs_properties_add_list(whisper_params_group, "whisper_sampling_method",
		MT_("whisper_sampling_method"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(whisper_sampling_method_list, "Beam search", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_property_list_add_int(whisper_sampling_method_list, "Greedy", WHISPER_SAMPLING_GREEDY);

	// int n_threads;
	obs_properties_add_int_slider(whisper_params_group, "n_threads", MT_("n_threads"), 1, 8, 1);
	// int n_max_text_ctx;     // max tokens to use from past text as prompt for the decoder
	obs_properties_add_int_slider(whisper_params_group, "n_max_text_ctx", MT_("n_max_text_ctx"), 0, 16384, 100);
	// int offset_ms;          // start offset in ms
	// int duration_ms;        // audio duration to process in ms
	// bool translate;
	obs_properties_add_bool(whisper_params_group, "translate", MT_("translate"));
	// bool no_context;        // do not use past transcription (if any) as initial prompt for the decoder
	obs_properties_add_bool(whisper_params_group, "no_context", MT_("no_context"));
	// bool single_segment;    // force single segment output (useful for streaming)
	obs_properties_add_bool(whisper_params_group, "single_segment", MT_("single_segment"));
	// bool print_special;     // print special tokens (e.g. <SOT>, <EOT>, <BEG>, etc.)
	obs_properties_add_bool(whisper_params_group, "print_special", MT_("print_special"));
	// bool print_progress;    // print progress information
	obs_properties_add_bool(whisper_params_group, "print_progress", MT_("print_progress"));
	// bool print_realtime;    // print results from within whisper.cpp (avoid it, use callback instead)
	obs_properties_add_bool(whisper_params_group, "print_realtime", MT_("print_realtime"));
	// bool print_timestamps;  // print timestamps for each text segment when printing realtime
	obs_properties_add_bool(whisper_params_group, "print_timestamps", MT_("print_timestamps"));
	// bool  token_timestamps; // enable token-level timestamps
	obs_properties_add_bool(whisper_params_group, "token_timestamps", MT_("token_timestamps"));
	// float thold_pt;         // timestamp token probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_pt", MT_("thold_pt"), 0.0f, 1.0f, 0.05f);
	// float thold_ptsum;      // timestamp token sum probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_ptsum", MT_("thold_ptsum"), 0.0f, 1.0f, 0.05f);
	// int   max_len;          // max segment length in characters
	obs_properties_add_int_slider(whisper_params_group, "max_len", MT_("max_len"), 0, 100, 1);
	// bool  split_on_word;    // split on word rather than on token (when used with max_len)
	obs_properties_add_bool(whisper_params_group, "split_on_word", MT_("split_on_word"));
	// int   max_tokens;       // max tokens per segment (0 = no limit)
	obs_properties_add_int_slider(whisper_params_group, "max_tokens", MT_("max_tokens"), 0, 100, 1);
	// bool speed_up;          // speed-up the audio by 2x using Phase Vocoder
	obs_properties_add_bool(whisper_params_group, "speed_up", MT_("speed_up"));
	// const char * initial_prompt;
	obs_properties_add_text(whisper_params_group, "initial_prompt", MT_("initial_prompt"), OBS_TEXT_DEFAULT);
	// bool suppress_blank
	obs_properties_add_bool(whisper_params_group, "suppress_blank", MT_("suppress_blank"));
	// bool suppress_non_speech_tokens
	obs_properties_add_bool(whisper_params_group, "suppress_non_speech_tokens", MT_("suppress_non_speech_tokens"));
	// float temperature
	obs_properties_add_float_slider(whisper_params_group, "temperature", MT_("temperature"), 0.0f, 1.0f, 0.05f);
	// float max_initial_ts
	obs_properties_add_float_slider(whisper_params_group, "max_initial_ts", MT_("max_initial_ts"), 0.0f, 1.0f, 0.05f);
	// float length_penalty
	obs_properties_add_float_slider(whisper_params_group, "length_penalty", MT_("length_penalty"), -1.0f, 1.0f, 0.1f);

	UNUSED_PARAMETER(data);
	return ppts;
}

void wyw_source_activate(void *data) {
	struct wyw_source_data *wf = static_cast<struct wyw_source_data *>(data);
	obs_log(LOG_INFO, "watch_your_words filter activated");
	wf->active = true;
}

void wyw_source_deactivate(void *data)
{
	struct wyw_source_data *wf = static_cast<struct wyw_source_data *>(data);
	obs_log(LOG_INFO, "watch_your_words filter deactivated");
	wf->active = false;
}