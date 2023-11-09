#include "source.h"
#include "source_data.h"
#include "whisper-processing.h"
#include "audio-processing.h"
#include "model-utils/model-downloader.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define do_log(level, format, ...)                 \
blog(level, "[word filter: '%s'] " format, \
	     obs_source_get_name(wf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

bool started;

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

 void copy_obs_audio_data(float **dest, float **src, size_t channels, size_t frames)
{
	for (int i = 0; i < channels; i++) {
		memcpy(dest[i], src[i], sizeof(float) * frames);
	}
}
uint64_t start=0, end=0;
double sin_val = 0.0;
struct obs_audio_data *wyw_source_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	const size_t channels = wf->channels;
	double rate = 1000.0 / wf->sample_rate;
	float **fdata = (float **)audio->data;
	if (!started) {
		started = true;
		wf->start_timestamp = audio->timestamp;
	}
	float **a = new float *[channels];
	for (int i = 0; i < channels; i++) {
		a[i] = new float[audio->frames];
	}
	copy_obs_audio_data(a, fdata, channels, audio->frames);
	struct pair_audio b = {a, audio->timestamp};
	wf->audio_buf.push_back(b);
	//obs_log(LOG_INFO, "timstamp: %llu)",audio->timestamp);
	if (audio->timestamp <= (wf->start_timestamp + (uint64_t)DELAY_SEC * 1000000000)) {
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
			if (temp.timestamp >= start && temp.timestamp <= end) {
				for (size_t c = 0; c < channels; c++) {
					for (size_t j = 0; j < audio->frames; j++) {
						sin_val += rate * M_PI * 2.0;
						if (sin_val > M_PI * 2.0)
							sin_val -= M_PI * 2.0;
						double wave = sin(sin_val);
						temp.data[c][j] =
							(float)(wave / 100.0);
					}
				}
			} else if (temp.timestamp > end) {
				std::lock_guard<std::mutex> lock(
					*wf->timestamp_queue_mutex);
				if (!wf->timestamp_queue.empty()) {
					edit_timestamp temp =
						wf->timestamp_queue.front();
					wf->timestamp_queue.pop();
					start = temp.start;
					end = temp.end;
				}
			}
			copy_obs_audio_data(fdata, temp.data, channels, audio->frames);
			//obs_log(LOG_INFO, "timstamp2: %llu)", audio->timestamp);
			wf->audio_buf.pop_front();
			for (int i = 0; i < channels; i++) {
				delete temp.data[i];
			}
			delete temp.data;
		}
	}
	
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
	if (!wf->output.empty()) {
		DetectionResultWithText result = wf->output.front();
		if (result.start_timestamp <= audio->timestamp - (uint64_t)DELAY_SEC * 1000000000) {
			if (wf->use_text_source) {
				if (!wf->text_source_mutex) {
					obs_log(LOG_ERROR, "text_source_mutex is null");
					return audio;
				}

				if (!wf->text_source) {
					// attempt to acquire a weak ref to the text source if it's yet available
					acquire_weak_text_source_ref(wf);
				}
				std::lock_guard<std::mutex> lock(
					*wf->text_source_mutex);

				if (!wf->text_source) {
					obs_log(LOG_ERROR, "text_source is null");
					return audio;
				}
				auto target = obs_weak_source_get_source(wf->text_source);
				if (!target) {
					obs_log(LOG_ERROR, "text_source target is null");
					return audio;
				}
				auto text_settings = obs_source_get_settings(target);
				obs_data_set_string(text_settings, "text", result.text.c_str());
				obs_source_update(target, text_settings);
				obs_source_release(target);
			}
			wf->output.pop();
		}
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

	{
		std::lock_guard<std::mutex> lock(*wf->edit_mutex);
		if (wf->edit == true) {
			wf->edit = false;
			wf->edit_thread_cv->notify_all();
		}
	}

	if (wf->edit_thread.joinable()) {
		wf->edit_thread.join();
	}

	if (wf->edit_mode) {
		bfree(wf->edit_mode);
		wf->edit_mode = nullptr;
	}

	if (wf->whisper_model_path) {
		bfree(wf->whisper_model_path);
		wf->whisper_model_path = nullptr;
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

	delete wf->audio_buf_mutex;
	delete wf->timestamp_queue_mutex;
	delete wf->edit_mutex;
	delete wf->edit_thread_cv;

	for (; !wf->audio_buf.empty();) {
		//obs_log(LOG_INFO, "audio_buf emptying.");
		struct pair_audio temp = wf->audio_buf.front();
		for (int i = 0; i < wf->channels; i++) {
			delete temp.data[i];
		}
		delete temp.data;
		wf->audio_buf.pop_front();
	}
	delete wf;
	obs_log(LOG_INFO,"watch-your-words source destroyed.");
}

#define is_lead_byte(c) \
	(((c)&0xe0) == 0xc0 || ((c)&0xf0) == 0xe0 || ((c)&0xf8) == 0xf0)
#define is_trail_byte(c) (((c)&0xc0) == 0x80)

inline int lead_byte_length(const uint8_t c)
{
	if ((c & 0xe0) == 0xc0) {
		return 2;
	} else if ((c & 0xf0) == 0xe0) {
		return 3;
	} else if ((c & 0xf8) == 0xf0) {
		return 4;
	} else {
		return 1;
	}
}

inline bool is_valid_lead_byte(const uint8_t *c)
{
	const int length = lead_byte_length(c[0]);
	if (length == 1) {
		return true;
	}
	if (length == 2 && is_trail_byte(c[1])) {
		return true;
	}
	if (length == 3 && is_trail_byte(c[1]) && is_trail_byte(c[2])) {
		return true;
	}
	if (length == 4 && is_trail_byte(c[1]) && is_trail_byte(c[2]) &&
	    is_trail_byte(c[3])) {
		return true;
	}
	return false;
}

void set_text_callback(struct wyw_source_data *wf, const DetectionResultWithText &resultIn)
{
	DetectionResultWithText result = resultIn;
	/*
	uint64_t now = now_ms();
	if (result.text.empty() || result.result != DETECTION_RESULT_SPEECH) {
		// check if we should clear the current sub depending on the minimum subtitle duration
		if ((now - wf->last_sub_render_time) > wf->min_sub_duration) {
			// clear the current sub, run an empty sub
			result.text = "";
		} else {
			// nothing to do, the incoming sub is empty
			return;
		}
	}
	wf->last_sub_render_time = now;*/
#ifdef _WIN32
	std::stringstream ss;
	uint8_t *c_str = (uint8_t *)result.text.c_str();
	for (size_t i = 0; i < result.text.size(); ++i) {
		if (is_lead_byte(c_str[i])) {
			if (c_str[i + 1] == 0xff) {
				c_str[i + 1] = 0x9f;
			}
			if (!is_valid_lead_byte(c_str + i)) {
				c_str[i] = c_str[i] - 0x20;
			}
		} else {
			if (c_str[i] >= 0xf8) {
				uint8_t buf_[4];
				buf_[0] = c_str[i] - 0x20;
				buf_[1] = c_str[i + 1];
				buf_[2] = c_str[i + 2];
				buf_[3] = c_str[i + 3];
				if (is_valid_lead_byte(buf_)) {
					c_str[i] = c_str[i] - 0x20;
				}
			}
		}
	}
	std::string str_copy = (char *)c_str;
#else
	std::string str_copy = result.text;
#endif
	for (std::string &word : wf->banlist) {
		for (edit_timestamp &temp : wf->token_result) { 
			if (temp.text.find(word) != std::string::npos) {
				std::lock_guard<std::mutex> lock(*wf->timestamp_queue_mutex);
				wf->timestamp_queue.push(temp);
				uint64_t a = (temp.end - temp.start);
				obs_log(LOG_INFO,
					"edit timestamp added t0: %llu, t1: %llu word: %s dura: %llu", temp.start, temp.end, word.c_str(), a);
			}
		}
	}
	wf->token_result.clear();
	/*
	if (wf->caption_to_stream) {
		obs_output_t *streaming_output = obs_frontend_get_streaming_output();
		if (streaming_output) {
			obs_output_output_caption_text1(streaming_output, str_copy.c_str());
			obs_output_release(streaming_output);
		}
	}*/

	if (wf->output_file_path != "" && !wf->text_source_name) {
		// Check if we should save the sentence
		if (!obs_frontend_recording_active()) {
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
			format_ts_for_srt(result.start_timestamp_ms + DELAY_SEC * 1000);
			output_file << " --> ";
			format_ts_for_srt(result.end_timestamp_ms + DELAY_SEC * 1000 - OVERLAP_SIZE_MSEC);
			output_file << std::endl;

			output_file << str_copy << std::endl;
			output_file << std::endl;
			output_file.close();
			wf->sentence_number++;
		}
	} else {
		wf->output.push(result);
	}
};

void wyw_source_update(void *data, obs_data_t *s)
{
	//obs_log(LOG_INFO, "watch-your-words source updating.");
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	obs_data_array_t *data_array = obs_data_get_array(s, "ban list");
	wf->banlist.clear();
	for (int i = 0; i < obs_data_array_count(data_array); i++) {
		obs_data_t *string = obs_data_array_item(data_array, i);
		const char *a = obs_data_get_string(string, "value");
		wf->banlist.push_back(std::string(a));
	}
	wf->channels = audio_output_get_channels(obs_get_audio());

	wf->vad_enabled = obs_data_get_bool(s, "vad_enabled");
	//wf->caption_to_stream = obs_data_get_bool(s, "caption_to_stream");
	//wf->step_size_msec = BUFFER_SIZE_MSEC;
	wf->save_srt = obs_data_get_bool(s, "subtitle_save_srt");
	// Get the current timestamp using the system clock
	wf->start_timestamp_ms = now_ms();
	wf->sentence_number = 1;
	wf->process_while_muted = obs_data_get_bool(s, "process_while_muted");

	const char *new_text_source_name = obs_data_get_string(s, "subtitle_sources");
	obs_weak_source_t *old_weak_text_source = NULL;

	if (new_text_source_name == nullptr ||
	    strcmp(new_text_source_name, "none") == 0 ||
	    strcmp(new_text_source_name, "(null)") == 0 ||
	    strcmp(new_text_source_name, "srt_file") == 0 ||
	    strlen(new_text_source_name) == 0) {
		wf->use_text_source = false;
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
		if (strcmp(new_text_source_name, "srt_file") == 0) {
			// set the output file path
			std::string current_path = obs_frontend_get_current_record_output_path();
			std::string tmp_path = "tmp.srt";
			wf->output_file_path = current_path + tmp_path;
		}
	} else {
		// new selected text source is valid, check if it's different from the old one
		wf->use_text_source = true;
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
	
	std::string new_edit_mode = obs_data_get_string(s, "edit_mode");
	if (wf->edit_mode == nullptr ||
		    strcmp(new_edit_mode.c_str(), wf->edit_mode) != 0) {
			shutdown_edit_thread(wf);
			wf->edit_mode = bstrdup(new_edit_mode.c_str());
			start_edit_thread(wf);
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

		std::string model_file_found =
			find_model_file(wf->whisper_model_path);

		if (model_file_found == "") {
			obs_log(LOG_ERROR, "Whisper model does not exist");
			download_model_with_ui_dialog(
				wf->whisper_model_path,
				[wf](int download_status, const std::string &path) {
					if (download_status == 0) {
						obs_log(LOG_INFO,
							"Model download complete");
						start_whisper_thread_with_path(wf, path);
					} else {
						obs_log(LOG_ERROR,
							"Model download failed");
					}
				});
		} else {
			start_whisper_thread_with_path(wf, model_file_found);
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
	wf->whisper_params.duration_ms = 3000;
	wf->whisper_params.language = "ko";
	wf->whisper_params.initial_prompt = "인터넷 방송";
	wf->whisper_params.n_threads = std::min((int)obs_data_get_int(s, "n_threads"),(int)std::thread::hardware_concurrency());
	wf->whisper_params.n_max_text_ctx = 16384;
	wf->whisper_params.translate = false;
	wf->whisper_params.no_context =true;
	wf->whisper_params.single_segment =true; 
	wf->whisper_params.print_special = false;
	wf->whisper_params.print_progress = false;
	wf->whisper_params.print_realtime = false;
	wf->whisper_params.print_timestamps = false;
	wf->whisper_params.token_timestamps = true;
	wf->whisper_params.thold_pt = (float)obs_data_get_double(s, "thold_pt");
	wf->whisper_params.thold_ptsum = (float)obs_data_get_double(s, "thold_ptsum");
	wf->whisper_params.max_len = 0;
	wf->whisper_params.split_on_word =true;
	wf->whisper_params.max_tokens = (int)obs_data_get_int(s, "max_tokens");
	wf->whisper_params.speed_up = false;
	wf->whisper_params.suppress_blank = true;
	wf->whisper_params.suppress_non_speech_tokens = true;
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
	//wf->step_size_msec = BUFFER_SIZE_MSEC;
	wf->save_srt = obs_data_get_bool(settings, "subtitle_save_srt");
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

	wf->audio_buf_mutex = new std::mutex();
	wf->timestamp_queue_mutex = new std::mutex();
	wf->edit_mutex = new std::mutex();
	wf->edit_thread_cv = new std::condition_variable();

	obs_log(LOG_INFO, "watch_your_words: clear text source data");
	wf->text_source = nullptr;
	const char *subtitle_sources = obs_data_get_string(settings, "subtitle_sources");
	if (subtitle_sources != nullptr) {
		wf->text_source_name = bstrdup(subtitle_sources);
	} else {
		wf->text_source_name = nullptr;
	}
	obs_log(LOG_INFO, "watch_your_words: clear paths and whisper context");
	wf->output_file_path = std::string("");
	wf->whisper_model_path = nullptr;
	wf->whisper_context = nullptr;
	wf->edit = false;

	wyw_source_update(wf, settings);

	wf->active = true;

	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void *private_data) {
			if (event == OBS_FRONTEND_EVENT_RECORDING_STARTING) {
				struct wyw_source_data *wf_ = static_cast<struct wyw_source_data*>(private_data);
				if (wf_->save_srt) {
					obs_log(LOG_INFO,"Recording started. Resetting srt file.");
					std::ofstream output_file(wf_->output_file_path, std::ios::out | std::ios::trunc);
					obs_log(LOG_INFO, "output_file_path is %s", wf_->output_file_path.c_str());
					output_file.close();
					wf_->sentence_number = 1;
					wf_->start_timestamp_ms = now_ms();
				}
			} else if (event == OBS_FRONTEND_EVENT_RECORDING_STOPPED) {
				struct wyw_source_data *wf_ = static_cast<struct wyw_source_data*>(private_data);
				if (wf_->save_srt) {
					obs_log(LOG_INFO, "Recording stopped. Rename srt file.");
					std::string recording_file_name = obs_frontend_get_last_recording();
					recording_file_name = recording_file_name.substr(0,recording_file_name.find_last_of("."));
					std::string srt_file_name = recording_file_name + ".srt";
					std::rename(wf_->output_file_path.c_str(), srt_file_name.c_str());
					obs_log(LOG_INFO, "srt_file_name is %s", srt_file_name.c_str());
				}}},wf);
	//obs_log(LOG_INFO, "watch-your-words source created.");
	return wf;
}

struct obs_source_frame *wyw_source_filter_video(void *data, struct obs_source_frame *frame)
{
	return frame;
}

void wyw_source_defaults(obs_data_t *s) {
	obs_log(LOG_INFO, "watch_your_words_defaults");

	obs_data_set_default_string(s, "edit_mode","beep");

	obs_data_set_default_bool(s, "vad_enabled", true);
	obs_data_set_default_string(s, "whisper_model_path", "models/ggml-medium-q5_0.bin");
	obs_data_set_default_string(s, "subtitle_sources", "none");
	obs_data_set_default_bool(s, "process_while_muted", false);
	obs_data_set_default_bool(s, "subtitle_save_srt", false);

	// Whisper parameters
	obs_data_set_default_int(s, "whisper_sampling_method", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_data_set_default_int(s, "n_threads",std::min(4,(int)std::thread::hardware_concurrency()));
	obs_data_set_default_double(s, "thold_pt", 0.01);
	obs_data_set_default_double(s, "thold_ptsum", 0.01);
	obs_data_set_default_int(s, "max_tokens", 32);
	obs_data_set_default_double(s, "temperature", 0.5);
	obs_data_set_default_double(s, "max_initial_ts", 1.0);
	obs_data_set_default_double(s, "length_penalty", -1.0);
}

obs_properties_t *wyw_source_properties(void *data)
{
	struct wyw_source_data *wf = static_cast<struct wyw_source_data *>(data);
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_editable_list(ppts,"ban list",MT_("ban list"),OBS_EDITABLE_LIST_TYPE_STRINGS,NULL, NULL);

	obs_property_t *ban_list_property = obs_properties_get(ppts, "ban list");

	obs_property_set_modified_callback2(
		ban_list_property,
		[](void *data, obs_properties_t *props,
		   obs_property_t *property, obs_data_t *settings) {
			obs_log(LOG_INFO, "ban_list modified");
			UNUSED_PARAMETER(property);
			UNUSED_PARAMETER(props);
			struct wyw_source_data *wf =
				static_cast<struct wyw_source_data *>(data);
			return true;
		},
		wf);

	obs_properties_add_bool(ppts, "vad_enabled", MT_("vad_enabled"));
	obs_properties_add_bool(ppts, "log_words", MT_("log_words"));
	//obs_properties_add_bool(ppts, "caption_to_stream", MT_("caption_to_stream"));
	obs_properties_add_bool(ppts, "process_while_muted", MT_("process_while_muted"));
	obs_property_t *subs_output = obs_properties_add_list(
		ppts, "subtitle_sources", MT_("subtitle_sources"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(subs_output, MT_("none_no_output"),
				     "none");
	obs_property_list_add_string(subs_output, MT_("srt_file_output"),
				     "srt_file");
	// Add text sources
	obs_enum_sources(add_sources_to_list, subs_output);

	obs_properties_add_bool(ppts, "subtitle_save_srt", MT_("save_srt"));

	obs_property_set_modified_callback(
		subs_output,[](obs_properties_t *props, obs_property_t *property,obs_data_t *settings) {
			UNUSED_PARAMETER(property);
			// Show or hide the output filename selection input
			const char *new_output = obs_data_get_string(settings, "subtitle_sources");
			const bool show_hide =(strcmp(new_output, "srt_file") == 0);
			obs_property_set_visible(obs_properties_get(props, "subtitle_save_srt"),show_hide);
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

	obs_property_t *edit_list = obs_properties_add_list(ppts, "edit_mode", MT_("edit mode"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(edit_list, "mute","mute");
	obs_property_list_add_string(edit_list, "beep","beep");

	obs_properties_t *whisper_params_group = obs_properties_create();
	obs_properties_add_group(ppts, "whisper_params_group", MT_("whisper_parameters"), OBS_GROUP_NORMAL, whisper_params_group);

	obs_property_t *whisper_sampling_method_list = obs_properties_add_list(whisper_params_group, "whisper_sampling_method",
		MT_("whisper_sampling_method"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(whisper_sampling_method_list, "Beam search", WHISPER_SAMPLING_BEAM_SEARCH);
	obs_property_list_add_int(whisper_sampling_method_list, "Greedy", WHISPER_SAMPLING_GREEDY);

	// int n_threads;
	obs_properties_add_int_slider(whisper_params_group, "n_threads", MT_("n_threads"), 1,(int)std::thread::hardware_concurrency(),1);
	// float thold_pt;         // timestamp token probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_pt", MT_("thold_pt"), 0.0f, 1.0f, 0.05f);
	// float thold_ptsum;      // timestamp token sum probability threshold (~0.01)
	obs_properties_add_float_slider(whisper_params_group, "thold_ptsum", MT_("thold_ptsum"), 0.0f, 1.0f, 0.05f);
	// int   max_tokens;       // max tokens per segment (0 = no limit)
	obs_properties_add_int_slider(whisper_params_group, "max_tokens", MT_("max_tokens"), 0, 100, 1);
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