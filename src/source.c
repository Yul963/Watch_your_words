#include "source.h"

struct obs_source_info watch_your_words_source = {
	.id = "WatchYourWord_source",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO | OBS_SOURCE_ASYNC, //OBS_SOURCE_ASYNC_VIDEO,
	.get_name = wyw_source_getname,
	.create = wyw_source_create,
	.destroy = wyw_source_destroy,
	.update = wyw_source_update,
	.filter_audio = wyw_source_filter_audio,
	//.filter_video = wyw_source_filter_video,
	.get_defaults = wyw_source_defaults,
	.get_properties = wyw_source_properties,
	.activate = wyw_source_activate,
	.deactivate = wyw_source_deactivate
};