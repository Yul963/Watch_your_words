﻿#include "whisper-processing.h"
#include "src/utils/util.h"
//std::ofstream fout;

std::vector<std::string> split_segment_text(const std::string &input)
{
	std::istringstream iss(input);
	std::vector<std::string> words;
	std::string word;
	while (iss >> word) {
		words.push_back(word);
	}
	return words;
}

struct whisper_context *init_whisper_context(const std::string &model_path)
{
	obs_log(LOG_INFO, "Loading whisper model from %s", model_path.c_str());
	//char *model_file_path = obs_module_file(model_path.c_str());
	//obs_log(LOG_INFO, "whisper model from %s", model_file_path);
	struct whisper_context *ctx = whisper_init_from_file(model_path.c_str());
	if (ctx == nullptr) {
		obs_log(LOG_ERROR, "Failed to load whisper model");
		return nullptr;
	}
	obs_log(LOG_INFO, "Whisper model loaded: %s", whisper_print_system_info());
	return ctx;
}

struct DetectionResultWithText run_whisper_inference(struct wyw_source_data *wf, const float *pcm32f_data, size_t pcm32f_size, uint64_t start_timestamp)
{
	obs_log(LOG_INFO, "%s: processing %d samples, %.3f sec, %d threads",
		__func__, int(pcm32f_size), float(pcm32f_size) / WHISPER_SAMPLE_RATE, wf->whisper_params.n_threads);
	std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
	if (wf->whisper_context == nullptr) {
		obs_log(LOG_WARNING, "whisper context is null");
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	}

	// set duration in ms
	const uint64_t duration_ms = (uint64_t)(pcm32f_size * 1000 / WHISPER_SAMPLE_RATE);
	// Get the duration in ms since the beginning of the stream (wf->start_timestamp_ms)
	const uint64_t offset_ms =(uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
				   std::chrono::system_clock::now().time_since_epoch()).count() - wf->start_timestamp_ms);

	// run the inference
	int whisper_full_result = -1;
	try {
		whisper_full_result = whisper_full(wf->whisper_context, wf->whisper_params, pcm32f_data, (int)pcm32f_size);
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Whisper exception: %s. Filter restart is required", e.what());
		whisper_free(wf->whisper_context);
		wf->whisper_context = nullptr;
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	}

	if (whisper_full_result != 0) {
		obs_log(LOG_WARNING, "failed to process audio, error %d", whisper_full_result);
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	} else {
		const int n_segment = 0;
		const char *text = whisper_full_get_segment_text(wf->whisper_context, n_segment);
		std::vector<std::string> words = split_segment_text(text);
		const int64_t t0 = offset_ms - duration_ms; //whisper_full_get_segment_t0(wf->whisper_context, n_segment); 
		const int64_t t1 = offset_ms;
		//whisper_full_get_segment_t1(wf->whisper_context, n_segment); 
		//offset_ms + duration_ms;
		std::vector<edit_timestamp> edit;
		int64_t word_t0 = 0;
		int64_t word_t1 = 0;//word timestamp
		float sentence_p = 0.0f;
		const int n_tokens = whisper_full_n_tokens(wf->whisper_context, n_segment);
		//std::vector<whisper_token_data> tokens(n_tokens);
		std::string word1, word2;
		//fout << "start_timestamp: " << start_timestamp << "\n";
		//for (int j = 0; j < n_tokens; ++j) {
		//	tokens[j] = whisper_full_get_token_data(wf->whisper_context, n_segment, j);
		//}
		//fout << " segment t0: "<< whisper_full_get_segment_t0(wf->whisper_context, n_segment)<< "\n";
		for (int j = 0; j < n_tokens; ++j) {
			//const auto &token = tokens[j];
			const auto token = whisper_full_get_token_data(wf->whisper_context, n_segment, j);
			//const float p = whisper_full_get_token_p(wf->whisper_context, n_segment, j);
			const float p = token.p;
			sentence_p += p;
			bool eot = token.id >= whisper_token_eot(wf->whisper_context);
			const char *txt = whisper_full_get_token_text(wf->whisper_context, n_segment, j);
			
			if (!eot) {
				word1 += txt;
				if (word1.empty() || word1[0] == '.') {
					word1.clear();
					continue;
				}
				if (word2.empty()) {
					word2 += word1;
					word1.clear();
					word_t0 = token.t0;
					word_t1 = token.t1;
				} else if (word1[0] == ' ') {
					edit.emplace_back(
						"",
						word_t0 * 10000000 +
							start_timestamp,
						word_t1 * 10000000 +
							start_timestamp); //ms to ns
					word2.clear();

					word2 += word1;
					word_t0 = token.t0;
					word_t1 = token.t1;
					word1.clear();
				}
				word2 += word1;
				if (token.t1 != 299)
					word_t1 = token.t1;
				word1.clear();
			}
			if ((n_tokens - 1) == j) {
				edit.emplace_back(
					"",
					word_t0 * 10000000 + start_timestamp,
					word_t1 * 10000000 +
						start_timestamp); //ms to ns
				word2.clear();
			}
			
					
		}
		sentence_p /= (float)n_tokens;

		// convert text to lowercase
		std::string text_lower(text);
		std::transform(text_lower.begin(), text_lower.end(), text_lower.begin(), ::tolower);
		// trim whitespace (use lambda)
		text_lower.erase(std::find_if(text_lower.rbegin(), text_lower.rend(),[](unsigned char ch) { return !std::isspace(ch); }).base(),text_lower.end());
		if (edit.size() == words.size()) {
			for (int i = 0; i < edit.size(); i++) {
				edit[i].text = words[i];
			}
			wf->token_result = edit;
		}
		else {
			obs_log(LOG_ERROR,
				"edit size %d, words size %d not match.",
				edit.size(), words.size());
		}
		//obs_log(LOG_INFO, "wisper segment: %d", whisper_full_n_segments(wf->whisper_context));
		obs_log(LOG_INFO, "[%s --> %s] (%.3f) %s", to_timestamp(t0).c_str(),
				to_timestamp(t1).c_str(), sentence_p, text_lower.c_str());
		if (text_lower.empty() || text_lower == ".") {
			return {DETECTION_RESULT_SILENCE, "", 0, 0};
		}

		return {DETECTION_RESULT_SPEECH, text_lower, offset_ms - duration_ms, offset_ms, start_timestamp};
	}
}

void process_whisper_audio_from_buffer(struct wyw_source_data *wf)
{
	uint32_t num_new_frames_from_infos = 0;
	uint64_t start_timestamp = 0;
	bool last_step_in_segment = false;
	{
		std::lock_guard<std::mutex> lock(*wf->stt_buf_mutex);
		// We need (wf->frames - wf->last_num_frames) new frames for a full segment,
		const size_t remaining_frames_to_full_segment = wf->frames - wf->last_num_frames;
		struct watch_your_words_audio_info info_from_buf = {0};
		const size_t size_of_audio_info = sizeof(struct watch_your_words_audio_info);
		while (wf->info_buffer.size >= size_of_audio_info) {
			circlebuf_pop_front(&wf->info_buffer, &info_from_buf, size_of_audio_info);
		//	fout << "info_buffer.size: " << wf->info_buffer.size
		//	     << " num_new_frames_from_infos: "
		//	     << num_new_frames_from_infos
		//	     << " remaining_frames_to_full_segment: "
		//	     << remaining_frames_to_full_segment << "\n";
			num_new_frames_from_infos += info_from_buf.frames;
			if (start_timestamp == 0) {
				start_timestamp = info_from_buf.timestamp;
			}
			// Check if we're within the needed segment length
			if (num_new_frames_from_infos > remaining_frames_to_full_segment) {
				// too big, push the last info into the buffer's front where it was
				num_new_frames_from_infos -= info_from_buf.frames;
				circlebuf_push_front(&wf->info_buffer, &info_from_buf, size_of_audio_info);
				last_step_in_segment = true; // this is the final step in the segment
				break;
			}
		}

		obs_log(LOG_INFO,
			"with %lu remaining to full segment, popped %d info-frames, pushing into buffer at %lu",
			remaining_frames_to_full_segment, num_new_frames_from_infos,
			wf->last_num_frames);

		for (size_t c = 0; c < wf->channels; c++) {
			// Push the new data to the end of the existing buffer copy_buffers[c]
			circlebuf_pop_front(&wf->input_buffers[c],
					    wf->copy_buffers[c] + wf->last_num_frames,
					    num_new_frames_from_infos * sizeof(float));
		//	fout << "pop count: " << c << "size: "
		//	     << num_new_frames_from_infos * sizeof(float)
		//	     << "\n";
		}
	}

	if (wf->last_num_frames > 0) {
		wf->last_num_frames += num_new_frames_from_infos;
		if (!last_step_in_segment) {
			// Mid-segment process
			obs_log(LOG_INFO, "mid-segment, now %d frames left to full segment",
				(int)(wf->frames - wf->last_num_frames));
		} else {
			// Final step in segment
			obs_log(LOG_INFO, "full segment, %d frames to process",
				(int)(wf->last_num_frames));
		}
	} else {
		wf->last_num_frames = wf->overlap_frames;
		obs_log(LOG_INFO, "first segment, %d frames to process",
			(int)(wf->last_num_frames));
	}

	obs_log(LOG_INFO, "processing %d frames (%d ms), start timestamp %llu ",
		(int)wf->last_num_frames, (int)(wf->last_num_frames * 1000 / wf->sample_rate), start_timestamp);

	auto start = std::chrono::high_resolution_clock::now();

	// resample to 16kHz
	float *output[MAX_AUDIO_CHANNELS];
	uint32_t out_frames;
	uint64_t ts_offset;
	audio_resampler_resample(wf->resampler, (uint8_t **)output, &out_frames, &ts_offset,
				 (const uint8_t **)wf->copy_buffers, (uint32_t)wf->last_num_frames);

	obs_log(LOG_INFO, "%d channels, %d frames, %f ms", (int)wf->channels, (int)out_frames,
		(float)out_frames / WHISPER_SAMPLE_RATE * 1000.0f);

	bool skipped_inference = false;

	if (wf->vad_enabled) {
		skipped_inference = !::vad_simple(output[0], out_frames, WHISPER_SAMPLE_RATE, VAD_THOLD,
			FREQ_THOLD, true);
	}

	if (!skipped_inference) {
		// run inference
		const struct DetectionResultWithText inference_result = run_whisper_inference(wf, output[0], out_frames, start_timestamp);

		if (inference_result.result == DETECTION_RESULT_SPEECH) {
			// output inference result to a text source
			set_text_callback(wf, inference_result);
		} else if (inference_result.result == DETECTION_RESULT_SILENCE) {
			// output inference result to a text source
			set_text_callback(wf,
					  {inference_result.result, "[silence]",0, 0, start_timestamp});
		}
	} else {
		set_text_callback(wf, {DETECTION_RESULT_UNKNOWN, "[skip]", 0, 0, start_timestamp});
	}

	auto end = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
	const uint64_t last_num_frames_ms = wf->last_num_frames * 1000 / wf->sample_rate;
	obs_log(LOG_INFO, "audio processing of %lu ms data took %d ms", last_num_frames_ms, (int)duration);

	if (last_step_in_segment) {
		for (size_t c = 0; c < wf->channels; c++) {
			// This is the last step in the segment - reset the copy buffer (include overlap frames)
			// move overlap frames from the end of the last copy_buffers to the beginning
			memcpy(wf->copy_buffers[c], wf->copy_buffers[c] + wf->last_num_frames - wf->overlap_frames, wf->overlap_frames * sizeof(float));
			// zero out the rest of the buffer, just in case
			memset(wf->copy_buffers[c] + wf->overlap_frames, 0, (wf->frames - wf->overlap_frames) * sizeof(float));
			wf->last_num_frames = wf->overlap_frames;
		}
	}
}

void whisper_loop(struct wyw_source_data *wf)
{
	//fout.open("C:/Users/dbfrb/Desktop/test.txt", std::ios::out | std::ios::binary);

	if (wf == nullptr) {
		obs_log(LOG_ERROR, "whisper_loop: data is null");
		return;
	}
	obs_log(LOG_INFO, "starting whisper thread");
	// Thread main loop
	while (true) {
		{
			std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
			if (wf->whisper_context == nullptr) {
				obs_log(LOG_WARNING, "Whisper context is null, exiting thread");
				break;
			}
		}
		while (true) {
			size_t input_buf_size = 0;
			{
				std::lock_guard<std::mutex> lock(*wf->stt_buf_mutex);
				input_buf_size = wf->input_buffers[0].size;
			}
			//const size_t step_size_frames = wf->step_size_msec * wf->sample_rate / 1000;
			const size_t segment_size = wf->frames * sizeof(float);
			//fout << "input_buf_size: " << input_buf_size
			//     << " segment_size: " << segment_size << "\n";
			if (input_buf_size >= segment_size) {
				obs_log(LOG_INFO,
					"found %lu bytes, %lu frames in input buffer, need >= %lu, processing",
					input_buf_size, (size_t)(input_buf_size / sizeof(float)), segment_size);

				// Process the audio. This will also remove the processed data from the input buffer.
				// Mutex is locked inside process_audio_from_buffer.
				process_whisper_audio_from_buffer(wf);
			} else {
				break;
			}
		}
		// Sleep for 10 ms using the condition variable wshiper_thread_cv
		// This will wake up the thread if there is new data in the input buffer
		// or if the whisper context is null
		std::unique_lock<std::mutex> lock(*wf->whisper_ctx_mutex);
		wf->stt_thread_cv->wait_for(lock, std::chrono::milliseconds(10));
	}
	//fout.close();
	obs_log(LOG_INFO, "exiting whisper thread");
}

void shutdown_whisper_thread(struct wyw_source_data *wf)
{
	obs_log(LOG_INFO, "shutdown_whisper_thread");
	if (wf->whisper_context != nullptr) {
		// acquire the mutex before freeing the context
		if (!wf->whisper_ctx_mutex || !wf->stt_thread_cv) {
			obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
			return;
		}
		std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
		whisper_free(wf->whisper_context);
		wf->whisper_context = nullptr;
		wf->stt_thread_cv->notify_all();
	}
	if (wf->stt_thread.joinable()) {
		wf->stt_thread.join();
	}
	if (wf->whisper_model_path != nullptr) {
		bfree(wf->whisper_model_path);
		wf->whisper_model_path = nullptr;
	}
}

void start_whisper_thread_with_path(struct wyw_source_data *wf,const std::string &path)
{
	obs_log(LOG_INFO, "start_whisper_thread_with_path: %s", path.c_str());
	if (!wf->whisper_ctx_mutex) {
		obs_log(LOG_ERROR,"cannot init whisper: whisper_ctx_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*wf->whisper_ctx_mutex);
	if (wf->whisper_context != nullptr) {
		obs_log(LOG_ERROR,"cannot init whisper: whisper_context is not null");
		return;
	}
	wf->whisper_context = init_whisper_context(path);
	std::thread new_whisper_thread(whisper_loop, wf);
	wf->stt_thread.swap(new_whisper_thread);
}
