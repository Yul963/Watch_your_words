#include <obs-module.h>
#include <obs.h>
#include <math.h>
#include <plugin-support.h>

#include "whisper.h"

#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

#define do_log(level, format, ...)                 \
	blog(level, "[word filter: '%s'] " format, \
	     obs_source_get_name(gf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

#define S_wyw_DB "db"

#define MT_ obs_module_text
#define TEXT_wyw_DB MT_("wyw.wywDB")

 whisper_token_data data;//just test

struct wyw_data {
	obs_source_t *context;
	size_t channels;
	float multiple;
};

static const char *wyw_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("WatchYourWord");
}

static void wyw_destroy(void *data)
{
	struct wyw_data *gf = (struct wyw_data *)data;
	bfree(gf);
}

static void wyw_update(void *data, obs_data_t *s)
{
	struct wyw_data *gf = (struct wyw_data *)data;
	double val = obs_data_get_double(s, S_wyw_DB);
	gf->channels = audio_output_get_channels(obs_get_audio());
	//gf->multiple = db_to_mul((float)val);
}

static void *wyw_create(obs_data_t *settings, obs_source_t *filter)
{
	struct wyw_data *gf = bzalloc(sizeof(*gf));
	gf->context = filter;
	wyw_update(gf, settings);
	return gf;
}

static double sin_val = 0.0;
static struct obs_audio_data *wyw_filter_audio(void *data,
						struct obs_audio_data *audio)
{
	double rate = 1000.0 / 48000.0;
	//struct obs_source_audio data;
	struct wyw_data *gf = (struct wyw_data *)data;
	const size_t channels = gf->channels;
	//uint8_t **adata = audio->data;
	float **fdata = (float **)audio->data;
	float multiple = gf->multiple;
	//uint64_t start = 1584359553172100, end = 1584359553172100;
	//obs_log(LOG_INFO, "watch-your-words test timstamp: %llu)", audio->timestamp);
	//if ((audio->timestamp > start) && (audio->timestamp < end)) {
		for (size_t c = 0; c < channels; c++) {
			if (audio->data[c]) {
				//obs_log(LOG_INFO,"watch-your-words test audio_data: %u)",adata[c][0]);
				for (size_t i = 0; i < audio->frames; i++) {
					sin_val += rate * M_PI * 2.0;
					if (sin_val > M_PI * 2.0)
						sin_val -= M_PI * 2.0;
					double wave = sin(sin_val);
					fdata[c][i] =(float)(wave/100.0);

					//obs_log(LOG_INFO,"audio_data: %f, %d)",fdata[c][i], i);
					/* multiple = adata[c][i];
					adata[c][i] = adata[c][audio->frames - i - 1];
					adata[c][audio->frames-i] = multiple;*/
					//adata[c][i] = 0.009929f;
				}
				//obs_log(LOG_INFO,"watch-your-words test audio_data: %u)",adata[c][0]);
			}
		}
	//}
	return audio;
}

static void wyw_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, S_wyw_DB, 0.0f);
}

static obs_properties_t *wyw_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *p = obs_properties_add_float_slider(
		ppts, S_wyw_DB, TEXT_wyw_DB, -30.0, 30.0, 0.1);
	obs_property_float_set_suffix(p, " dB");

	obs_properties_add_editable_list(
		ppts,
		"ban list",
		MT_("ban list"),
		OBS_EDITABLE_LIST_TYPE_STRINGS,
		NULL, NULL);

	UNUSED_PARAMETER(data);
	return ppts;
}

struct obs_source_info gain_filter = {
	.id = "WatchYourWord",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = wyw_name,
	.create = wyw_create,
	.destroy = wyw_destroy,
	.update = wyw_update,
	.filter_audio = wyw_filter_audio,
	.get_defaults = wyw_defaults,
	.get_properties = wyw_properties,
};