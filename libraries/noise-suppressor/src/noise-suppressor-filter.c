#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <media-io/audio-io.h>
#include <media-io/audio-resampler.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/deque.h>
#include <util/threading.h>

#include "noise-suppressor-dsp.h"

#define FILTER_ID "obs_noise_suppressor_filter"
#define NS_MAX_CHANNELS NS_DSP_MAX_CHANNELS
#define BUFFER_MSEC 10u
#define TIMESTAMP_RESET_NS 1000000000ULL

#define S_PRESET "preset"
#define S_PRESET_APPLIED "preset_applied"
#define S_SUPPRESSION "suppression_strength"
#define S_GATE_FLOOR "gate_floor"
#define S_ATTACK "attack_ms"
#define S_HOLD "hold_ms"
#define S_RELEASE "release_ms"
#define S_HIGHPASS "highpass_enabled"
#define S_HIGHPASS_HZ "highpass_hz"
#define S_OUTPUT_GAIN "output_gain"
#define S_LIMITER "limiter_enabled"
#define S_CEILING "limiter_ceiling"
#define S_BYPASS "bypass"

#define PRESET_QUALITY "quality"
#define PRESET_BALANCED "balanced"
#define PRESET_PERFORMANCE "performance"

#define TEXT_FILTER_NAME "FilterName"
#define TEXT_DESCRIPTION "Description"
#define TEXT_PRESET "Preset"
#define TEXT_PRESET_QUALITY "Preset.Quality"
#define TEXT_PRESET_BALANCED "Preset.Balanced"
#define TEXT_PRESET_PERFORMANCE "Preset.Performance"
#define TEXT_PRESET_TT "Preset.ToolTip"
#define TEXT_SUPPRESSION "Suppression"
#define TEXT_GATE_FLOOR "GateFloor"
#define TEXT_ATTACK "GateAttack"
#define TEXT_HOLD "GateHold"
#define TEXT_RELEASE "GateRelease"
#define TEXT_HIGHPASS "Highpass"
#define TEXT_HIGHPASS_HZ "HighpassFrequency"
#define TEXT_OUTPUT_GAIN "OutputGain"
#define TEXT_LIMITER "Limiter"
#define TEXT_CEILING "LimiterCeiling"
#define TEXT_BYPASS "Bypass"
#define TEXT_SUPPRESSION_TT "Suppression.ToolTip"
#define TEXT_GATE_FLOOR_TT "GateFloor.ToolTip"
#define TEXT_ATTACK_TT "GateAttack.ToolTip"
#define TEXT_HOLD_TT "GateHold.ToolTip"
#define TEXT_RELEASE_TT "GateRelease.ToolTip"
#define TEXT_HIGHPASS_TT "Highpass.ToolTip"
#define TEXT_HIGHPASS_HZ_TT "HighpassFrequency.ToolTip"
#define TEXT_OUTPUT_GAIN_TT "OutputGain.ToolTip"
#define TEXT_LIMITER_TT "Limiter.ToolTip"
#define TEXT_CEILING_TT "LimiterCeiling.ToolTip"
#define TEXT_BYPASS_TT "Bypass.ToolTip"

typedef struct ns_audio_info {
	uint32_t frames;
	uint64_t timestamp;
} ns_audio_info;

typedef struct ns_filter_data {
	obs_source_t *context;
	pthread_mutex_t mutex;
	uint32_t sample_rate;
	size_t channels;
	size_t native_frames;
	uint64_t last_timestamp;
	uint64_t latency;
	bool bypass;
	bool configured;
	ns_dsp_config config;
	ns_dsp_channel dsp[NS_MAX_CHANNELS];
	struct deque input[NS_MAX_CHANNELS];
	struct deque output[NS_MAX_CHANNELS];
	struct deque info;
	float *native_buffers[NS_MAX_CHANNELS];
	float *rnnoise_buffers[NS_MAX_CHANNELS];
	float *output_data;
	audio_resampler_t *to_rnnoise;
	audio_resampler_t *from_rnnoise;
	struct obs_audio_data output_audio;
} ns_filter_data;

static float clampf(float value, float low, float high)
{
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

static enum speaker_layout speaker_layout_for_channels(size_t channels)
{
	switch (channels) {
	case 1:
		return SPEAKERS_MONO;
	case 2:
		return SPEAKERS_STEREO;
	case 3:
		return SPEAKERS_2POINT1;
	case 4:
		return SPEAKERS_4POINT0;
	case 5:
		return SPEAKERS_4POINT1;
	case 6:
		return SPEAKERS_5POINT1;
	case 8:
		return SPEAKERS_7POINT1;
	default:
		return SPEAKERS_UNKNOWN;
	}
}

static ns_dsp_config config_defaults(void)
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

static void apply_preset(ns_dsp_config *config, const char *preset)
{
	if (strcmp(preset, PRESET_QUALITY) == 0) {
		config->suppression_strength = 0.92f;
		config->gate_floor = 0.05f;
		config->attack_ms = 10.0f;
		config->hold_ms = 90.0f;
		config->release_ms = 220.0f;
	} else if (strcmp(preset, PRESET_PERFORMANCE) == 0) {
		config->suppression_strength = 0.72f;
		config->gate_floor = 0.14f;
		config->attack_ms = 5.0f;
		config->hold_ms = 50.0f;
		config->release_ms = 130.0f;
	}
}

static void apply_preset_to_settings(obs_data_t *settings, const char *preset)
{
	ns_dsp_config config = config_defaults();
	apply_preset(&config, preset);
	obs_data_set_double(settings, S_SUPPRESSION, config.suppression_strength);
	obs_data_set_double(settings, S_GATE_FLOOR, config.gate_floor);
	obs_data_set_double(settings, S_ATTACK, config.attack_ms);
	obs_data_set_double(settings, S_HOLD, config.hold_ms);
	obs_data_set_double(settings, S_RELEASE, config.release_ms);
}

static ns_dsp_config read_config(obs_data_t *settings)
{
	ns_dsp_config config = config_defaults();
	const char *preset = obs_data_get_string(settings, S_PRESET);
	if (preset && *preset)
		apply_preset(&config, preset);

	if (obs_data_has_user_value(settings, S_SUPPRESSION))
		config.suppression_strength = (float)obs_data_get_double(settings, S_SUPPRESSION);
	if (obs_data_has_user_value(settings, S_GATE_FLOOR))
		config.gate_floor = (float)obs_data_get_double(settings, S_GATE_FLOOR);
	if (obs_data_has_user_value(settings, S_ATTACK))
		config.attack_ms = (float)obs_data_get_double(settings, S_ATTACK);
	if (obs_data_has_user_value(settings, S_HOLD))
		config.hold_ms = (float)obs_data_get_double(settings, S_HOLD);
	if (obs_data_has_user_value(settings, S_RELEASE))
		config.release_ms = (float)obs_data_get_double(settings, S_RELEASE);
	if (obs_data_has_user_value(settings, S_HIGHPASS))
		config.highpass_enabled = obs_data_get_bool(settings, S_HIGHPASS);
	if (obs_data_has_user_value(settings, S_HIGHPASS_HZ))
		config.highpass_hz = (float)obs_data_get_double(settings, S_HIGHPASS_HZ);
	if (obs_data_has_user_value(settings, S_OUTPUT_GAIN))
		config.output_gain = (float)obs_data_get_double(settings, S_OUTPUT_GAIN);
	if (obs_data_has_user_value(settings, S_LIMITER))
		config.limiter_enabled = obs_data_get_bool(settings, S_LIMITER);
	if (obs_data_has_user_value(settings, S_CEILING))
		config.limiter_ceiling = (float)obs_data_get_double(settings, S_CEILING);
	return config;
}

static void clear_buffers(ns_filter_data *filter)
{
	for (size_t channel = 0; channel < filter->channels; ++channel) {
		deque_pop_front(&filter->input[channel], NULL, filter->input[channel].size);
		deque_pop_front(&filter->output[channel], NULL, filter->output[channel].size);
	}
	deque_pop_front(&filter->info, NULL, filter->info.size);
}

static void free_processing(ns_filter_data *filter)
{
	size_t channels = filter->channels < NS_MAX_CHANNELS ? filter->channels : NS_MAX_CHANNELS;
	for (size_t channel = 0; channel < channels; ++channel) {
		ns_dsp_channel_free(&filter->dsp[channel]);
		deque_free(&filter->input[channel]);
		deque_free(&filter->output[channel]);
	}
	deque_free(&filter->info);
	if (filter->to_rnnoise)
		audio_resampler_destroy(filter->to_rnnoise);
	if (filter->from_rnnoise)
		audio_resampler_destroy(filter->from_rnnoise);
	bfree(filter->native_buffers[0]);
	bfree(filter->rnnoise_buffers[0]);
	bfree(filter->output_data);
	memset(filter->native_buffers, 0, sizeof(filter->native_buffers));
	memset(filter->rnnoise_buffers, 0, sizeof(filter->rnnoise_buffers));
	filter->to_rnnoise = NULL;
	filter->from_rnnoise = NULL;
	filter->configured = false;
}

static bool setup_processing(ns_filter_data *filter)
{
	if (!filter->sample_rate || !filter->channels || filter->channels > NS_MAX_CHANNELS)
		return false;

	filter->native_frames = filter->sample_rate / BUFFER_MSEC / 10u;
	if (!filter->native_frames)
		return false;

	filter->latency = 1000000000ULL / (1000u / BUFFER_MSEC);
	filter->native_buffers[0] = bzalloc(filter->native_frames * filter->channels * sizeof(float));
	filter->rnnoise_buffers[0] = bzalloc(NS_DSP_FRAME_SIZE * filter->channels * sizeof(float));
	if (!filter->native_buffers[0] || !filter->rnnoise_buffers[0]) {
		free_processing(filter);
		return false;
	}

	deque_init(&filter->info);
	for (size_t channel = 0; channel < filter->channels; ++channel) {
		filter->native_buffers[channel] = filter->native_buffers[0] + channel * filter->native_frames;
		filter->rnnoise_buffers[channel] = filter->rnnoise_buffers[0] + channel * NS_DSP_FRAME_SIZE;
		deque_init(&filter->input[channel]);
		deque_init(&filter->output[channel]);
		deque_reserve(&filter->input[channel], filter->native_frames * sizeof(float) * 8u);
		deque_reserve(&filter->output[channel], filter->native_frames * sizeof(float) * 8u);
		if (!ns_dsp_channel_init(&filter->dsp[channel])) {
				free_processing(filter);
				return false;
			}
		ns_dsp_channel_set_config(&filter->dsp[channel], &filter->config);
	}
	deque_reserve(&filter->info, sizeof(ns_audio_info) * 8u);

	if (filter->sample_rate != NS_DSP_SAMPLE_RATE) {
		struct resample_info source = {
			.samples_per_sec = filter->sample_rate,
			.format = AUDIO_FORMAT_FLOAT_PLANAR,
			.speakers = speaker_layout_for_channels(filter->channels),
		};
		struct resample_info destination = {
			.samples_per_sec = NS_DSP_SAMPLE_RATE,
			.format = AUDIO_FORMAT_FLOAT_PLANAR,
			.speakers = speaker_layout_for_channels(filter->channels),
		};
		filter->to_rnnoise = audio_resampler_create(&destination, &source);
		filter->from_rnnoise = audio_resampler_create(&source, &destination);
		if (!filter->to_rnnoise || !filter->from_rnnoise) {
				free_processing(filter);
				return false;
			}
	}
	filter->output_audio.data[0] = NULL;
	filter->configured = true;
	return true;
}

static void update_config(ns_filter_data *filter, obs_data_t *settings)
{
	pthread_mutex_lock(&filter->mutex);
	uint32_t sample_rate = audio_output_get_sample_rate(obs_get_audio());
	size_t channels = audio_output_get_channels(obs_get_audio());
	bool topology_changed = sample_rate != filter->sample_rate || channels != filter->channels;
	filter->config = read_config(settings);
	filter->bypass = obs_data_get_bool(settings, S_BYPASS);
	if (topology_changed && filter->configured)
		free_processing(filter);
	filter->sample_rate = sample_rate;
	filter->channels = channels;
	if (!filter->configured)
		setup_processing(filter);
	else
		for (size_t channel = 0; channel < filter->channels; ++channel)
			ns_dsp_channel_set_config(&filter->dsp[channel], &filter->config);
	pthread_mutex_unlock(&filter->mutex);
}

static void process_block(ns_filter_data *filter)
{
	for (size_t channel = 0; channel < filter->channels; ++channel)
		deque_pop_front(&filter->input[channel], filter->native_buffers[channel], filter->native_frames * sizeof(float));

	if (filter->to_rnnoise) {
		const uint8_t *input[NS_MAX_CHANNELS];
		uint8_t *output[NS_MAX_CHANNELS];
		uint32_t output_frames = 0;
		uint64_t timestamp = 0;
		for (size_t channel = 0; channel < filter->channels; ++channel)
			input[channel] = (const uint8_t *)filter->native_buffers[channel];

		if (!audio_resampler_resample(filter->to_rnnoise, output, &output_frames, &timestamp, input,
					(uint32_t)filter->native_frames))
			goto push_output;

		for (size_t channel = 0; channel < filter->channels; ++channel) {
			float *resampled = (float *)output[channel];
			for (size_t sample = 0; sample < NS_DSP_FRAME_SIZE; ++sample) {
				int64_t source = (int64_t)output_frames - (int64_t)NS_DSP_FRAME_SIZE + (int64_t)sample;
				filter->rnnoise_buffers[channel][sample] = source >= 0 && source < (int64_t)output_frames ? resampled[source] : 0.0f;
			}
			ns_dsp_process_frame(&filter->dsp[channel], filter->rnnoise_buffers[channel], &filter->config);
			input[channel] = (const uint8_t *)filter->rnnoise_buffers[channel];
		}

		if (!audio_resampler_resample(filter->from_rnnoise, output, &output_frames, &timestamp, input,
					NS_DSP_FRAME_SIZE))
			goto push_output;

		for (size_t channel = 0; channel < filter->channels; ++channel) {
			float *resampled = (float *)output[channel];
			for (size_t sample = 0; sample < filter->native_frames; ++sample) {
				int64_t source = (int64_t)output_frames - (int64_t)filter->native_frames + (int64_t)sample;
				filter->native_buffers[channel][sample] = source >= 0 && source < (int64_t)output_frames ? resampled[source] : 0.0f;
			}
		}
	} else {
		for (size_t channel = 0; channel < filter->channels; ++channel)
			ns_dsp_process_frame(&filter->dsp[channel], filter->native_buffers[channel], &filter->config);
	}

push_output:
	for (size_t channel = 0; channel < filter->channels; ++channel)
		deque_push_back(&filter->output[channel], filter->native_buffers[channel], filter->native_frames * sizeof(float));
}

static struct obs_audio_data *filter_audio(void *data, struct obs_audio_data *audio)
{
	ns_filter_data *filter = data;
	if (!filter || !audio)
		return audio;

	pthread_mutex_lock(&filter->mutex);
	if (filter->bypass || !filter->configured || !filter->channels) {
		pthread_mutex_unlock(&filter->mutex);
		return audio;
	}

	if (filter->last_timestamp && llabs((int64_t)filter->last_timestamp - (int64_t)audio->timestamp) > (int64_t)TIMESTAMP_RESET_NS) {
		clear_buffers(filter);
		for (size_t channel = 0; channel < filter->channels; ++channel)
			ns_dsp_channel_reset(&filter->dsp[channel]);
	}
	filter->last_timestamp = audio->timestamp;

	ns_audio_info info = {.frames = audio->frames, .timestamp = audio->timestamp};
	deque_push_back(&filter->info, &info, sizeof(info));
	for (size_t channel = 0; channel < filter->channels; ++channel)
		deque_push_back(&filter->input[channel], audio->data[channel], audio->frames * sizeof(float));

	size_t block_bytes = filter->native_frames * sizeof(float);
	while (filter->input[0].size >= block_bytes)
		process_block(filter);

	if (filter->info.size < sizeof(info) || filter->output[0].size < info.frames * sizeof(float)) {
		pthread_mutex_unlock(&filter->mutex);
		return NULL;
	}
	deque_peek_front(&filter->info, &info, sizeof(info));
	if (filter->output[0].size < info.frames * sizeof(float)) {
		pthread_mutex_unlock(&filter->mutex);
		return NULL;
	}
	deque_pop_front(&filter->info, NULL, sizeof(info));

	size_t output_size = info.frames * sizeof(float) * filter->channels;
	filter->output_data = brealloc(filter->output_data, output_size);
	for (size_t channel = 0; channel < filter->channels; ++channel) {
		filter->output_audio.data[channel] = (uint8_t *)filter->output_data + channel * info.frames * sizeof(float);
		deque_pop_front(&filter->output[channel], filter->output_audio.data[channel], info.frames * sizeof(float));
	}
	filter->output_audio.frames = info.frames;
	filter->output_audio.timestamp = info.timestamp > filter->latency ? info.timestamp - filter->latency : 0;
	pthread_mutex_unlock(&filter->mutex);
	return &filter->output_audio;
}

static const char *filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "StreamHub — Supressor de ruído (RNNoise)";
}

static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, S_PRESET, PRESET_BALANCED);
	obs_data_set_default_double(settings, S_SUPPRESSION, 0.85);
	obs_data_set_default_double(settings, S_GATE_FLOOR, 0.08);
	obs_data_set_default_double(settings, S_ATTACK, 8.0);
	obs_data_set_default_double(settings, S_HOLD, 70.0);
	obs_data_set_default_double(settings, S_RELEASE, 180.0);
	obs_data_set_default_bool(settings, S_HIGHPASS, true);
	obs_data_set_default_double(settings, S_HIGHPASS_HZ, 80.0);
	obs_data_set_default_double(settings, S_OUTPUT_GAIN, 1.0);
	obs_data_set_default_bool(settings, S_LIMITER, true);
	obs_data_set_default_double(settings, S_CEILING, 0.95);
	obs_data_set_default_bool(settings, S_BYPASS, false);
}

static obs_properties_t *filter_properties(void *unused)
{
	UNUSED_PARAMETER(unused);
	obs_properties_t *properties = obs_properties_create();
	obs_property_t *preset = obs_properties_add_list(properties, S_PRESET, "Predefinição", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(preset, "Qualidade", PRESET_QUALITY);
	obs_property_list_add_string(preset, "Equilibrado", PRESET_BALANCED);
	obs_property_list_add_string(preset, "Desempenho", PRESET_PERFORMANCE);
	obs_properties_add_float_slider(properties, S_SUPPRESSION, "Força da redução de ruído", 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(properties, S_GATE_FLOOR, "Nível mínimo do gate", 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(properties, S_ATTACK, "Abertura do gate (ms)", 1.0, 80.0, 1.0);
	obs_properties_add_float_slider(properties, S_HOLD, "Sustentação do gate (ms)", 0.0, 500.0, 5.0);
	obs_properties_add_float_slider(properties, S_RELEASE, "Fechamento do gate (ms)", 20.0, 1000.0, 5.0);
	obs_properties_add_bool(properties, S_HIGHPASS, "Remover ruído grave (vento/manuseio)");
	obs_properties_add_float_slider(properties, S_HIGHPASS_HZ, "Frequência do filtro passa-altas (Hz)", 20.0, 300.0, 5.0);
	obs_properties_add_float_slider(properties, S_OUTPUT_GAIN, "Ganho de saída", 0.0, 2.0, 0.01);
	obs_properties_add_bool(properties, S_LIMITER, "Limitador de volume");
	obs_properties_add_float_slider(properties, S_CEILING, "Limite máximo", 0.5, 1.0, 0.01);
	obs_properties_add_bool(properties, S_BYPASS, "Desativar processamento (bypass)");
	return properties;
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	ns_filter_data *filter = bzalloc(sizeof(*filter));
	filter->context = source;
	pthread_mutex_init_value(&filter->mutex);
	filter->config = config_defaults();
	update_config(filter, settings);
	return filter;
}

static void filter_update(void *data, obs_data_t *settings)
{
	update_config(data, settings);
}

static void filter_destroy(void *data)
{
	ns_filter_data *filter = data;
	if (!filter)
		return;
	pthread_mutex_lock(&filter->mutex);
	free_processing(filter);
	pthread_mutex_unlock(&filter->mutex);
	pthread_mutex_destroy(&filter->mutex);
	bfree(filter);
}

struct obs_source_info obs_noise_suppressor_filter = {
	.id = FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_AUDIO,
	.get_name = filter_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.update = filter_update,
	.filter_audio = filter_audio,
	.get_defaults = filter_defaults,
	.get_properties = filter_properties,
};

void streamhub_register_noise_suppressor(void)
{
	obs_register_source(&obs_noise_suppressor_filter);
	blog(LOG_INFO, "[streamhub] filtro supressor de ruido RNNoise registrado");
}
