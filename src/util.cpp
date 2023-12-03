#include "util.h"
#include <plugin-support.h>
#include <obs.h>
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif

std::string to_timestamp(int64_t t)
{
	int64_t sec = t / 1000;
	int64_t msec = t - sec * 1000;
	int64_t min = sec / 60;
	sec = sec - min * 60;

	char buf[32];
	snprintf(buf, sizeof(buf), "%02d:%02d.%03d", (int)min, (int)sec,
		 (int)msec);

	return std::string(buf);
}

void high_pass_filter(float *pcmf32, size_t pcm32f_size, float cutoff,
		      uint32_t sample_rate)
{
	const float rc = 1.0f / (2.0f * (float)M_PI * cutoff);
	const float dt = 1.0f / (float)sample_rate;
	const float alpha = dt / (rc + dt);
	float y = pcmf32[0];
	for (size_t i = 1; i < pcm32f_size; i++) {
		y = alpha * (y + pcmf32[i] - pcmf32[i - 1]);
		pcmf32[i] = y;
	}
}

// VAD (voice activity detection), return true if speech detected
bool vad_simple(float *pcmf32, size_t pcm32f_size, uint32_t sample_rate,
		float vad_thold, float freq_thold, bool verbose)
{
	const uint64_t n_samples = pcm32f_size;
	if (freq_thold > 0.0f) {
		high_pass_filter(pcmf32, pcm32f_size, freq_thold, sample_rate);
	}
	float energy_all = 0.0f;
	for (uint64_t i = 0; i < n_samples; i++) {
		energy_all += fabsf(pcmf32[i]);
	}
	energy_all /= (float)n_samples;
	if (verbose) {
		obs_log(LOG_INFO,
			"%s: energy_all: %f, vad_thold: %f, freq_thold: %f",
			__func__, energy_all, vad_thold, freq_thold);
	}
	if (energy_all < vad_thold) {
		return false;
	}
	return true;
}