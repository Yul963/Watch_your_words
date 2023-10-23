#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif
void wyw_source_activate(void *data);
void *wyw_source_create(obs_data_t *settings, obs_source_t *filter);
void wyw_source_update(void *data, obs_data_t *s);
void wyw_source_destroy(void *data);
const char *wyw_source_getname(void *unused);
struct obs_audio_data *wyw_source_filter_audio(void *data, struct obs_audio_data *audio);
//struct obs_source_frame *wyw_source_filter_video(void *data, struct obs_source_frame *frame);
void wyw_source_deactivate(void *data);
void wyw_source_defaults(obs_data_t *s);
obs_properties_t *wyw_source_properties(void *data);
#ifdef __cplusplus
}
#endif