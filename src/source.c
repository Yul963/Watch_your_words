#include <obs-module.h>
#include <obs.h>
#include <math.h>

#include "whisper.h"
#define do_log(level, format, ...)                 \
	blog(level, "[gain filter: '%s'] " format, \
	     obs_source_get_name(gf->context), ##__VA_ARGS__)

#define warn(format, ...) do_log(LOG_WARNING, format, ##__VA_ARGS__)
#define info(format, ...) do_log(LOG_INFO, format, ##__VA_ARGS__)

#define S_GAIN_DB "db"

#define MT_ obs_module_text
#define TEXT_GAIN_DB MT_("Gain.GainDB")

 whisper_token_data data;//just test

struct gain_data {
	obs_source_t *context;
	size_t channels;
	float multiple;
};

static const char *gain_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return MT_("WatchYourWord");
}

static void gain_destroy(void *data)
{
	struct gain_data *gf = (struct gain_data *)data;
	bfree(gf);
}

static void gain_update(void *data, obs_data_t *s)
{
	struct gain_data *gf = (struct gain_data *)data;
	double val = obs_data_get_double(s, S_GAIN_DB);
	gf->channels = audio_output_get_channels(obs_get_audio());
	//gf->multiple = db_to_mul((float)val);
}

static void *gain_create(obs_data_t *settings, obs_source_t *filter)
{
	struct gain_data *gf = bzalloc(sizeof(*gf));
	gf->context = filter;
	gain_update(gf, settings);
	return gf;
}

static struct obs_audio_data *gain_filter_audio(void *data,
						struct obs_audio_data *audio)
{
	struct gain_data *gf = (struct gain_data *)data;
	const size_t channels = gf->channels;
	float **adata = (float **)audio->data;
	const float multiple = gf->multiple;

	for (size_t c = 0; c < channels; c++) {
		if (audio->data[c]) {
			for (size_t i = 0; i < audio->frames; i++) {
				adata[c][i] *= multiple;
			}
		}
	}

	return audio;
}

static void gain_defaults(obs_data_t *s)
{
	obs_data_set_default_double(s, S_GAIN_DB, 0.0f);
}

static obs_properties_t *gain_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();

	obs_property_t *p = obs_properties_add_float_slider(
		ppts, S_GAIN_DB, TEXT_GAIN_DB, -30.0, 30.0, 0.1);
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
	.get_name = gain_name,
	.create = gain_create,
	.destroy = gain_destroy,
	.update = gain_update,
	.filter_audio = gain_filter_audio,
	.get_defaults = gain_defaults,
	.get_properties = gain_properties,
};
/*
struct obs_source_info source = {
	.id = "source",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = source_name,
	.create = source_create,
	.destroy = source_destroy,
	.update = source_update,
	.video_render = source_render,
	.get_width = source_width,
	.get_height = source_height
};*/