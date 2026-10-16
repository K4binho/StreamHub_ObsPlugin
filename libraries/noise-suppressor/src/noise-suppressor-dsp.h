#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <rnnoise.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NS_DSP_SAMPLE_RATE 48000u
#define NS_DSP_FRAME_SIZE 480u
#define NS_DSP_MAX_CHANNELS 8u

typedef struct ns_dsp_config {
	float suppression_strength;
	float gate_floor;
	float attack_ms;
	float hold_ms;
	float release_ms;
	bool highpass_enabled;
	float highpass_hz;
	float output_gain;
	bool limiter_enabled;
	float limiter_ceiling;
} ns_dsp_config;

typedef struct ns_dsp_channel {
	DenoiseState *rnnoise;
	float vad;
	float gate_gain;
	unsigned int hold_frames;
	float hp_input;
	float hp_output;
	float hp_alpha;
} ns_dsp_channel;

bool ns_dsp_channel_init(ns_dsp_channel *channel);
void ns_dsp_channel_free(ns_dsp_channel *channel);
void ns_dsp_channel_reset(ns_dsp_channel *channel);
void ns_dsp_channel_set_config(ns_dsp_channel *channel, const ns_dsp_config *config);
void ns_dsp_process_frame(ns_dsp_channel *channel, float *samples, const ns_dsp_config *config);

#ifdef __cplusplus
}
#endif
