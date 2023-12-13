#include "audio_delay.h"

struct obs_source_info audio_delay = {
	.id = "WatchYourWord_delay_audio",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = wyw_audio_delay_getname,
	.create = wyw_audio_delay_create,
	.destroy = wyw_audio_delay_destroy,
	.filter_audio = wyw_audio_delay_filter_audio,
};