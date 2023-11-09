#include "audio_delay.h"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <deque>
#include <memory>

#define MT_ obs_module_text
#define DELAY_SEC 6

struct pair_audio {
	float **data;
	uint64_t timestamp;
};

bool audio_delay_started;
struct wyw_audio_delay_data {
	obs_source_t *context;
	size_t channels;
	uint64_t start_timestamp;
	std::deque<struct pair_audio> audio_buf;
};

 extern void copy_obs_audio_data(float **dest, float **src, size_t channels, size_t frames);

struct obs_audio_data *wyw_audio_delay_filter_audio(void *data, struct obs_audio_data *audio)
{
	struct wyw_audio_delay_data *wf = (struct wyw_audio_delay_data *)data;
	const size_t channels = wf->channels;
	float **fdata = (float **)audio->data;
	if (!audio_delay_started) {
		audio_delay_started = true;
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
			copy_obs_audio_data(fdata, temp.data, channels, audio->frames);
			wf->audio_buf.pop_front();
			for (int i = 0; i < channels; i++) {
				delete temp.data[i];
			}
			delete temp.data;
		}
	}
	return audio;
}

const char *wyw_audio_delay_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("delay_audio");
}

void wyw_audio_delay_destroy(void *data)
{
	struct wyw_audio_delay_data *wf = (struct wyw_audio_delay_data *)data;

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

void *wyw_audio_delay_create(obs_data_t *settings, obs_source_t *filter)
{
	audio_delay_started = false;
	struct wyw_audio_delay_data *wf = new wyw_audio_delay_data();
	wf->context = filter;
	wf->channels = audio_output_get_channels(obs_get_audio());;
	return wf;
}