#ifndef UTIL_H
#define UTIL_H
#include <string>
#define VAD_THOLD 0.0001f
#define FREQ_THOLD 100.0f

std::string to_timestamp(int64_t t);

void high_pass_filter(float *pcmf32, size_t pcm32f_size, float cutoff,
		      uint32_t sample_rate);

// VAD (voice activity detection), return true if speech detected
bool vad_simple(float *pcmf32, size_t pcm32f_size, uint32_t sample_rate,
		float vad_thold, float freq_thold, bool verbose);

#endif