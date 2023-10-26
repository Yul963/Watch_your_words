#ifndef WHISPER_PROCESSING_H
#define WHISPER_PROCESSING_H
#include "source_data.h"
// buffer size in msec
#define BUFFER_SIZE_MSEC 3000
// at 16Khz, BUFFER_SIZE_MSEC is WHISPER_FRAME_SIZE samples
#define WHISPER_FRAME_SIZE 48000
// overlap in msec
#define OVERLAP_SIZE_MSEC 100

void whisper_loop(struct wyw_source_data *wf);
struct whisper_context *init_whisper_context(const std::string &model_path);
void shutdown_whisper_thread(struct wyw_source_data *wf);
void start_whisper_thread_with_path(struct wyw_source_data *wf, const std::string &path);

#endif // WHISPER_PROCESSING_H
