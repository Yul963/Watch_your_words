#include "google-stt-processing.h"
#include <iostream>
#include <vector>
#include <memory>
#include <utility>
#include <string>
#include <sstream>
#include "src/util.h"
#include <google/cloud/speech/speech_client.h>
#include <google/cloud/common_options.h>
namespace speech = ::google::cloud::speech;

struct DetectionResultWithText run_google_speech_inference(struct wyw_source_data *wf,
						     const uint8_t *pcm32_data, size_t pcm32_size, uint64_t start_timestamp)
{
	const uint64_t duration_ms = (uint64_t)(pcm32_size / wf->bytes_per_channel * 1000 / 16000);
	const uint64_t offset_ms = (uint64_t)(std::chrono::duration_cast<std::chrono::milliseconds>
		(std::chrono::system_clock::now().time_since_epoch()).count() - wf->start_timestamp_ms);
	std::string text;
	float sentence_p = 0.0f;
	const int64_t t0 = offset_ms - duration_ms;
	const int64_t t1 = offset_ms;
	std::vector<edit_timestamp> edit;
	try {
		auto client = wf->google_context;
		speech::v1::RecognizeRequest request;
		request.mutable_config()->set_encoding(
			speech::v1::RecognitionConfig_AudioEncoding::
				RecognitionConfig_AudioEncoding_LINEAR16);
		request.mutable_config()->set_sample_rate_hertz(16000);
		request.mutable_config()->set_language_code("ko-KR");
		request.mutable_config()->set_enable_word_time_offsets(true);
		request.mutable_config()->set_profanity_filter(false);
		request.mutable_config()->set_max_alternatives(0);

		std::string audio_chunk((char *)pcm32_data, pcm32_size);

		request.mutable_audio()->set_content(audio_chunk);
		//request.mutable_audio()->mutable_content()->assign(std::move(audio_chunk));

		obs_log(LOG_INFO, "starting recognition");
		auto client_response = client->Recognize(request);
		obs_log(LOG_INFO, "recognition ended");

		if (!client_response)throw std::move(client_response).status();
		else {
			for (auto const &result : client_response->results()) {
				obs_log(LOG_INFO, "results_size: %d",client_response->results_size());
				for (auto const &alternative : result.alternatives()) {
					obs_log(LOG_INFO,"alternatives_size: %d",result.alternatives_size());
					sentence_p = alternative.confidence();
					text = alternative.transcript();
					for (auto const &wordinfo : alternative.words()) {
						obs_log(LOG_INFO,"words_size: %d", alternative.words_size());
						std::string word(wordinfo.word());
						obs_log(LOG_INFO, "word: %s", word.c_str());
						google::protobuf::Duration start =wordinfo.start_time();
						google::protobuf::Duration end =wordinfo.end_time();
						obs_log(LOG_INFO,
							"word time: %d.%d - %d.%d",
							start.seconds(), start.nanos() / 100000000L,
							end.seconds(), end.nanos() / 100000000L);
						uint64_t t0 =start.seconds() *1000000000L +start.nanos();
						uint64_t t1 =end.seconds() *1000000000L +end.nanos();
						edit.emplace_back(word,t0 + start_timestamp,t1 + start_timestamp);
					}
				}
			}
			wf->token_result = edit;
		}
	}
	catch (const std::exception &ex){
		obs_log(LOG_ERROR,"recognize exception catch %s",ex.what());
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	}
	catch (const std::string &ex){
		obs_log(LOG_ERROR, "recognize exception string %s",ex.c_str());
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	} 
	catch (grpc::Status &s) {
		obs_log(LOG_INFO, "Recognize failed with:  %s, %d, %s",
			s.error_message().c_str(), s.error_code(),
			s.error_details().c_str());
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	} 
	catch (google::cloud::v2_18::Status const &s) {
		obs_log(LOG_INFO, "Recognize failed with:  %s, %d, %s",
			s.message().c_str(), s.code(), s.error_info().domain().c_str());
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	}
	catch (...){
		obs_log(LOG_ERROR, "recognize exception any error");
		return {DETECTION_RESULT_UNKNOWN, "", 0, 0};
	}
	
	
	std::string text_lower(text);
	std::transform(text_lower.begin(), text_lower.end(),
			    text_lower.begin(), ::tolower);
	text_lower.erase(std::find_if(text_lower.rbegin(),
					    text_lower.rend(),[](unsigned char ch) {
						    return !std::isspace(ch);}).base(),text_lower.end());

	obs_log(LOG_INFO, "[%s --> %s] (%.3f) %s",
		to_timestamp(t0).c_str(), to_timestamp(t1).c_str(),
		sentence_p, text_lower.c_str());
	if (text_lower.empty() || text_lower == ".") {
		return {DETECTION_RESULT_SILENCE, "", 0, 0};
	}

	return {DETECTION_RESULT_SPEECH, text_lower,
		offset_ms - duration_ms, offset_ms, start_timestamp};
}

void process_audio_from_buffer(struct wyw_source_data *wf)
{
	uint32_t num_new_frames_from_infos = 0;
	uint64_t start_timestamp = 0;
	bool last_step_in_segment = false;
	{
		std::lock_guard<std::mutex> lock(*wf->stt_buf_mutex);
		const size_t remaining_frames_to_full_segment = wf->frames - wf->last_num_frames;
		struct watch_your_words_audio_info info_from_buf = {0};
		const size_t size_of_audio_info = sizeof(struct watch_your_words_audio_info);
		while (wf->info_buffer.size >= size_of_audio_info) {
			circlebuf_pop_front(&wf->info_buffer, &info_from_buf,size_of_audio_info);
			num_new_frames_from_infos += info_from_buf.frames;
			if (start_timestamp == 0) {
				start_timestamp = info_from_buf.timestamp;
			}
			if (num_new_frames_from_infos > remaining_frames_to_full_segment) {
				num_new_frames_from_infos -= info_from_buf.frames;
				circlebuf_push_front(&wf->info_buffer, &info_from_buf, size_of_audio_info);
				last_step_in_segment = true;
				break;
			}
		}

		obs_log(LOG_INFO,
			"with %lu remaining to full segment, popped %d info-frames, pushing into buffer at %lu",
			remaining_frames_to_full_segment, num_new_frames_from_infos, wf->last_num_frames);

		for (size_t c = 0; c < wf->channels; c++) {
			circlebuf_pop_front(
				&wf->input_buffers[c], wf->copy_buffers[c] + wf->last_num_frames, num_new_frames_from_infos * sizeof(float));
		}
	}

	if (wf->last_num_frames > 0) {
		wf->last_num_frames += num_new_frames_from_infos;
		if (!last_step_in_segment) {
			obs_log(LOG_INFO,
				"mid-segment, now %d frames left to full segment",
				(int)(wf->frames - wf->last_num_frames));
		} else {
			obs_log(LOG_INFO, "full segment, %d frames to process",
				(int)(wf->last_num_frames));
		}
	} else {
		wf->last_num_frames = wf->overlap_frames;
		obs_log(LOG_INFO, "first segment, %d frames to process",
			(int)(wf->last_num_frames));
	}

	obs_log(LOG_INFO, "processing %d frames (%d ms), start timestamp %llu ",
		(int)wf->last_num_frames,
		(int)(wf->last_num_frames * 1000 / wf->sample_rate),
		start_timestamp);

	auto start = std::chrono::high_resolution_clock::now();

	// resample to 16kHz
	uint8_t *output[MAX_AUDIO_CHANNELS];
	memset(output, 0, sizeof(output));
	uint32_t out_frames;
	uint64_t ts_offset;
	audio_resampler_resample(wf->resampler, output, &out_frames,
				 &ts_offset, (const uint8_t **)wf->copy_buffers,
				 (uint32_t)wf->last_num_frames);
	
	obs_log(LOG_INFO, "%d channels, %d frames, %f ms", (int)wf->channels,
		(int)out_frames, (float)out_frames / 16000 * 1000.0f);
	out_frames = out_frames * wf->bytes_per_channel;
	bool skipped_inference = false;

	//if (wf->vad_enabled) {
	//	skipped_inference = !::vad_simple((float *)output[0], out_frames, 16000, VAD_THOLD, FREQ_THOLD, true);
	//}

	if (!skipped_inference) {
		const struct DetectionResultWithText inference_result =
			run_google_speech_inference(wf, output[0],out_frames,start_timestamp);

		if (inference_result.result == DETECTION_RESULT_SPEECH) {
			set_text_callback(wf, inference_result);
		} else if (inference_result.result == DETECTION_RESULT_SILENCE) {
			set_text_callback(wf, {inference_result.result, "[silence]", 0, 0, start_timestamp});
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
			memcpy(wf->copy_buffers[c], wf->copy_buffers[c] + wf->last_num_frames - wf->overlap_frames,
			       wf->overlap_frames * sizeof(float));
			memset(wf->copy_buffers[c] + wf->overlap_frames, 0, (wf->frames - wf->overlap_frames) * sizeof(float));
			wf->last_num_frames = wf->overlap_frames;
		}
	}
}

void google_stt_loop(struct wyw_source_data *wf)
{
	if (wf == nullptr) {
		obs_log(LOG_ERROR, "google_stt_loop: data is null");
		return;
	}
	obs_log(LOG_INFO, "starting google stt thread");
	while (true) {
		{
			std::lock_guard<std::mutex> lock(*wf->google_context_mutex);
			if (wf->google_context == nullptr) {
				obs_log(LOG_WARNING, "google-stt context is null, exiting thread");
				break;
			}
		}
		while (true) {
			size_t input_buf_size = 0;
			{
				std::lock_guard<std::mutex> lock(*wf->stt_buf_mutex);
				input_buf_size = wf->input_buffers[0].size;
			}
			const size_t segment_size = wf->frames * sizeof(float);
			if (input_buf_size >= segment_size) {
				obs_log(LOG_INFO,
					"found %lu bytes, %lu frames in input buffer, need >= %lu, processing",
					input_buf_size, (size_t)(input_buf_size / sizeof(float)), segment_size);
				process_audio_from_buffer(wf);
			} else {
				break;
			}
		}
		std::unique_lock<std::mutex> lock(*wf->google_context_mutex);
		wf->stt_thread_cv->wait_for(lock,std::chrono::milliseconds(10));
	}
	obs_log(LOG_INFO, "exiting google stt thread");
}

void shutdown_google_stt_thread(struct wyw_source_data *wf)
{
	obs_log(LOG_INFO, "shutdown_whisper_thread");
	if (wf->google_context != nullptr) {
		if (!wf->google_context_mutex || !wf->stt_thread_cv) {
			obs_log(LOG_ERROR, "whisper_ctx_mutex is null");
			return;
		}
		std::lock_guard<std::mutex> lock(*wf->google_context_mutex);
		delete wf->google_context;
		wf->google_context = nullptr;
		wf->stt_thread_cv->notify_all();
	}
	if (wf->stt_thread.joinable()) {
		wf->stt_thread.join();
	}
}

void start_google_stt_thread(struct wyw_source_data *wf)
{
	if (!wf->google_context_mutex) {
		obs_log(LOG_ERROR, "cannot init google_stt: googleSettings_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*wf->google_context_mutex);
	if (wf->google_context != nullptr) {
		obs_log(LOG_ERROR, "cannot init google_stt: google_context is not null");
		return;
	}

	wf->google_context = new speech::SpeechClient(speech::MakeSpeechConnection());
	std::thread new_google_stt_thread(google_stt_loop, wf);
	wf->stt_thread.swap(new_google_stt_thread);
}