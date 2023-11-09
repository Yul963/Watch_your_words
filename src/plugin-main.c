#include <obs-module.h>
#include <plugin-support.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "filtering curses.";
}

extern struct obs_source_info watch_your_words_source;
extern struct obs_source_info audio_delay;
extern struct obs_source_info video_delay;

bool obs_module_load(void)
{
	obs_register_source(&watch_your_words_source);
	obs_register_source(&audio_delay);
	obs_register_source(&video_delay);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)",
		PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
