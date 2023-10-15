#include "source.h"
#include "source_data.h"
#include <plugin-support.h>

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define do_log(level, format, ...)                 \
	blog(level, "[word filter: '%s'] " format, \
	     obs_source_get_name(wf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)
#define MT_ obs_module_text

bool started;

 void copy_obs_audio_data(float** dest, float **src, size_t channels)
{
	for (int i = 0; i < channels; i++) {
		memcpy(dest[i], src[i], sizeof(float)*480);
	}
}

const char *wyw_source_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("WatchYourWord");
}

void wyw_source_destroy(void *data)
{
	struct wyw_source_data *wf = (struct wyw_source_data *)data;

	for (; !wf->audio_buf.empty();) {
		obs_log(LOG_INFO, "audio_buf emptying.");
		struct pair_audio temp = wf->audio_buf.front();
		for (int i = 0; i < wf->channels; i++) {
			delete temp.data[i];
		}
		delete temp.data;
		wf->audio_buf.pop_front();
	}
	//pthread_mutex_destroy(&wf->mutex);
	bfree(wf);
	obs_log(LOG_INFO,"watch-your-words source destroyed.");
}

void wyw_source_update(void *data, obs_data_t *s)
{
	//obs_log(LOG_INFO, "watch-your-words source updating.");
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	obs_data_array_t *data_array = obs_data_get_array(s, "ban list");
	for (int i = 0; i < obs_data_array_count(data_array); i++) {
		obs_data_t *string = obs_data_array_item(data_array, i);
		const char *a = obs_data_get_string(string, "value");
		wf->words.push_back(std::string(a));
	}
	wf->channels = audio_output_get_channels(obs_get_audio());
}

void *wyw_source_create(obs_data_t *settings, obs_source_t *filter)
{
	started = false;
	struct wyw_source_data *wf = new wyw_source_data();
	wf->context = filter;
	wyw_source_update(wf, settings);
	//pthread_mutex_init_value(&wf->mutex);
	//obs_log(LOG_INFO, "watch-your-words source created.");
	return wf;
}

void checkBanList() {
	
}

void setTimestamp() {

}

double sin_val = 0.0;
struct obs_audio_data *wyw_source_filter_audio(void *data, struct obs_audio_data *audio)
{
	obs_log(LOG_INFO, "filter start");
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
			obs_log(LOG_INFO, "timstamp2: %llu)", audio->timestamp);
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
		for (int i=0; i< temp_q.size(); i++) {
			if ((temp_q[i].timestamp > temp_t.start) && (temp_q[i].timestamp < temp_t.end)) {
				for (size_t c = 0; c < channels; c++) {
					if (audio->data[c]) {
						for (size_t j = 0; j < audio->frames; i++) {
							sin_val += rate * M_PI * 2.0;
							if (sin_val > M_PI * 2.0)
								sin_val -= M_PI * 2.0;
							double wave = sin(sin_val);
							temp_q[i].data[c][j] =(float)(wave /100.0);
					
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
	obs_log(LOG_INFO, "filter end");
	return audio;
}
/*
static struct obs_source_frame *wyw_source_filter_video(void *data, struct obs_source_frame *frame)
{
	return frame;
}*/

void wyw_source_defaults(obs_data_t *s) {
	UNUSED_PARAMETER(s);
}

obs_properties_t *wyw_source_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_properties_add_editable_list(
		ppts,
		"ban list",
		MT_("ban list"),
		OBS_EDITABLE_LIST_TYPE_STRINGS,
		NULL, NULL);

	UNUSED_PARAMETER(data);
	return ppts;
}
/*
void wyw_source_activate(void *data) {

}

void wyw_source_deactivate(void* data) {
	struct wyw_source_data *wf = (struct wyw_source_data *)data;
	started = false;
	for (int i = 0; i < wf->audio_buf.size(); i++)
		delete_obs_audio_data(&(wf->audio_buf[i]), wf->channels);
	wf->audio_buf.clear();
*/