#include "noise-suppressor-dsp.h"

#include <math.h>
#include <string.h>

#define NS_VAD_ON 0.58f
#define NS_PI 3.14159265358979323846f

static float clampf(float value, float low, float high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static float frame_coefficient(float milliseconds)
{
	float duration_ms = (float)NS_DSP_FRAME_SIZE * 1000.0f / (float)NS_DSP_SAMPLE_RATE;
	float duration = duration_ms / fmaxf(milliseconds, 0.1f);
	return 1.0f - expf(-duration);
}

bool ns_dsp_channel_init(ns_dsp_channel *channel)
{
	if (!channel)
		return false;

	memset(channel, 0, sizeof(*channel));
	channel->rnnoise = rnnoise_create(NULL);
	if (channel->rnnoise)
		channel->gate_gain = 1.0f;
	return channel->rnnoise != NULL;
}

void ns_dsp_channel_free(ns_dsp_channel *channel)
{
	if (!channel)
		return;

	if (channel->rnnoise)
		rnnoise_destroy(channel->rnnoise);
	memset(channel, 0, sizeof(*channel));
}

void ns_dsp_channel_reset(ns_dsp_channel *channel)
{
	if (!channel)
		return;

	if (channel->rnnoise)
		rnnoise_destroy(channel->rnnoise);
	channel->rnnoise = rnnoise_create(NULL);
	channel->vad = 0.0f;
	channel->gate_gain = 1.0f;
	channel->hold_frames = 0;
	channel->hp_input = 0.0f;
	channel->hp_output = 0.0f;
}

void ns_dsp_channel_set_config(ns_dsp_channel *channel, const ns_dsp_config *config)
{
	if (!channel || !config)
		return;

	float hz = clampf(config->highpass_hz, 20.0f, 300.0f);
	channel->hp_alpha = expf(-2.0f * NS_PI * hz / (float)NS_DSP_SAMPLE_RATE);
}

static float highpass(ns_dsp_channel *channel, float sample)
{
	float output = channel->hp_alpha * (channel->hp_output + sample - channel->hp_input);
	channel->hp_input = sample;
	channel->hp_output = output;
	return output;
}

static float gate_target(ns_dsp_channel *channel, const ns_dsp_config *config)
{
	unsigned int hold_frames = (unsigned int)fmaxf(config->hold_ms, 0.0f) / 10u;
	if (channel->vad >= NS_VAD_ON) {
		channel->hold_frames = hold_frames;
		return 1.0f;
	}
	if (channel->hold_frames > 0) {
		--channel->hold_frames;
		return 1.0f;
	}
	return clampf(config->gate_floor, 0.0f, 1.0f);
}

void ns_dsp_process_frame(ns_dsp_channel *channel, float *samples, const ns_dsp_config *config)
{
	if (!channel || !samples || !config || !channel->rnnoise)
		return;

	float rnnoise_frame[NS_DSP_FRAME_SIZE];
	for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i) {
		float sample = samples[i];
		if (!isfinite(sample))
			sample = 0.0f;
		sample = clampf(sample, -1.0f, 1.0f);
		if (config->highpass_enabled)
			sample = highpass(channel, sample);
		rnnoise_frame[i] = sample * 32768.0f;
	}

	float vad = rnnoise_process_frame(channel->rnnoise, rnnoise_frame, rnnoise_frame);
	if (!isfinite(vad))
		vad = 0.0f;
	channel->vad += (clampf(vad, 0.0f, 1.0f) - channel->vad) * 0.35f;

	float target = gate_target(channel, config);
	float attack = frame_coefficient(config->attack_ms);
	float release = frame_coefficient(config->release_ms);
	float coefficient = target > channel->gate_gain ? attack : release;
	channel->gate_gain += (target - channel->gate_gain) * coefficient;

	float suppression = clampf(config->suppression_strength, 0.0f, 1.0f);
	float gain = clampf(config->output_gain, 0.0f, 4.0f);
	float ceiling = clampf(config->limiter_ceiling, 0.1f, 1.0f);
	for (size_t i = 0; i < NS_DSP_FRAME_SIZE; ++i) {
		float denoised = rnnoise_frame[i] / 32768.0f;
		float output = samples[i] * (1.0f - suppression) + denoised * suppression;
		output *= channel->gate_gain * gain;
		if (config->limiter_enabled)
			output = tanhf(output / ceiling) * ceiling;
		else
			output = clampf(output, -1.0f, 1.0f);
		samples[i] = isfinite(output) ? output : 0.0f;
	}
}
