#include "audio-processing.h"

double sin_val = 0.0;
double rate = 1000.0 / 48000.0;
void edit_audio_buffer(struct wyw_source_data *wf)
{
	const size_t channels = wf->channels;
	pair_audio pair = wf->audio_buf.front();
	if (!wf->timestamp_queue.empty()) {
		struct edit_timestamp temp_t = wf->timestamp_queue.front();
		wf->timestamp_queue.pop();
		std::deque<struct pair_audio> temp_q = wf->audio_buf;
		for (int i = 0; i < temp_q.size(); i++) {
			if ((temp_q[i].timestamp > temp_t.start) &&
			    (temp_q[i].timestamp < temp_t.end)) {
				for (size_t c = 0; c < channels; c++) {
					if (pair.data[c]) {
						for (size_t j = 0;
						     j < wf->frames; i++) {
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
}

void edit_loop(struct wyw_source_data *wf)
{
	if (wf == nullptr) {
		obs_log(LOG_ERROR, " void edit_loop(void *data): data is null");
		return;
	}
	obs_log(LOG_INFO, "starting edit thread");
	while (true) {
		{
			std::lock_guard<std::mutex> lock(*wf->edit_mutex);
			if (wf->edit == false) {
				obs_log(LOG_WARNING,"edit is false, exiting thread");
				break;
			}
		}
		while (true) {
			size_t input_buf_size = 0;
			{
				std::lock_guard<std::mutex> lock(*wf->audio_buf_mutex);
				
			}
			if (wf->audio_buf.empty())
				break;
			else
				break;
		}
		std::unique_lock<std::mutex> lock(*wf->edit_mutex);
		wf->edit_thread_cv->wait_for(lock,std::chrono::milliseconds(10));
	}
	obs_log(LOG_INFO, "exiting edit thread");
}

void shutdown_edit_thread(struct wyw_source_data* wf) {
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

	if (wf->edit_mode != nullptr) {
		bfree(wf->edit_mode);
		wf->edit_mode = nullptr;
	}
}

void start_edit_thread(struct wyw_source_data* wf) {
	if (!wf->edit_mutex) {
		obs_log(LOG_ERROR,"cannot init edit: edit_mode_mutex is null");
		return;
	}
	std::lock_guard<std::mutex> lock(*wf->edit_mutex);
	if (wf->edit == true) {
		obs_log(LOG_ERROR,"cannot init edit: edit_mode is not null");
		return;
	}
	wf->edit = true;
	std::thread new_edit_thread(edit_loop, wf);
	wf->edit_thread.swap(new_edit_thread);
}