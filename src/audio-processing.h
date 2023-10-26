#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H
#include "source_data.h"

void edit_loop(struct wyw_source_data *wf);
void shutdown_edit_thread(struct wyw_source_data *wf);
void start_edit_thread(struct wyw_source_data *wf);

#endif