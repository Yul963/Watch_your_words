#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif
void *wyw_audio_delay_create(obs_data_t *settings, obs_source_t *filter);
void wyw_audio_delay_destroy(void *data);
const char *wyw_audio_delay_getname(void *unused);
struct obs_audio_data *wyw_audio_delay_filter_audio(void *data, struct obs_audio_data *audio);
#ifdef __cplusplus
}
#endif