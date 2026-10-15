#include "noise-suppressor-dsp.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static ns_dsp_config default_config(void)
{
	ns_dsp_config config = {
		.suppression_strength = 0.85f,
		.gate_floor = 0.08f,
		.attack_ms = 8.0f,
		.hold_ms = 70.0f,
		.release_ms = 180.0f,
		.highpass_enabled = true,
		.highpass_hz = 80.0f,
		.output_gain = 1.0f,
		.limiter_enabled = true,
		.limiter_ceiling = 0.95f,
	};
	return config;
}

static void test_silence_and_bounds(void)
{
	ns_dsp_channel channel;
	ns_dsp_config config = default_config();
	float frame[NS_DSP_FRAME_SIZE];

	bool initialized = ns_dsp_channel_init(&channel);
	assert(initialized);
	if (!initialized)
		return;
	ns_dsp_channel_set_config(&channel, &config);
	memset(frame, 0, sizeof(frame));
	ns_dsp_process_frame(&channel, frame, &config);
	for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i)
		assert(isfinite(frame[i]) && fabsf(frame[i]) <= config.limiter_ceiling + 0.001f);

	for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i)
		frame[i] = 4.0f;
	ns_dsp_process_frame(&channel, frame, &config);
	for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i)
		assert(isfinite(frame[i]) && fabsf(frame[i]) <= 1.0f);

	ns_dsp_channel_free(&channel);
}

static void test_noise_reduction_is_finite(void)
{
	ns_dsp_channel channel;
	ns_dsp_config config = default_config();
	float frame[NS_DSP_FRAME_SIZE];
	float energy_before = 0.0f;
	float energy_after = 0.0f;

	bool initialized = ns_dsp_channel_init(&channel);
	assert(initialized);
	if (!initialized)
		return;
	ns_dsp_channel_set_config(&channel, &config);
	for (unsigned int block = 0; block < 20; ++block) {
		for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i) {
			float sample = 0.12f * sinf((float)(i + block * NS_DSP_FRAME_SIZE) * 0.37f);
			frame[i] = sample;
			energy_before += sample * sample;
		}
		ns_dsp_process_frame(&channel, frame, &config);
		for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i) {
			assert(isfinite(frame[i]));
			energy_after += frame[i] * frame[i];
		}
	}
	assert(energy_after >= 0.0f);
	assert(energy_before > 0.0f);
	ns_dsp_channel_free(&channel);
}

int main(void)
{
	test_silence_and_bounds();
	test_noise_reduction_is_finite();
	puts("DSP tests passed");
	return 0;
}
