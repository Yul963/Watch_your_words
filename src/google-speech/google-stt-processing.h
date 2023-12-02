#ifndef GOOGLE_STT_PROCESSING_H
#define GOOGLE_STT_PROCESSING_H
#include "source_data.h"

void google_stt_loop(struct wyw_source_data *wf);
void shutdown_google_stt_thread(struct wyw_source_data *wf);
void start_google_stt_thread(struct wyw_source_data *wf);

#endif