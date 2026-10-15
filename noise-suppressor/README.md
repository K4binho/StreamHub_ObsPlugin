# OBS Noise Suppressor

OBS audio filter DLL using RNNoise plus adaptive voice gate, high-pass rumble removal and output limiter.

Not Discord proprietary code. Behavior target: low-latency voice cleanup for common microphones.

## Build

From repository root:

```powershell
cmake --preset windows-x64 -S noise-suppressor
cmake --build --preset windows-x64 --config RelWithDebInfo
ctest --test-dir build_noise_suppressor -C RelWithDebInfo --output-on-failure
```

DLL output:

```text
build_noise_suppressor/RelWithDebInfo/obs-noise-suppressor.dll
```

Install into OBS `obs-plugins/64bit`. Keep `LICENSE-RNNOISE.txt` with distributed binary.

## Filter order

Use after microphone source. Start with `Balanced`. Add compressor after filter when stream loudness needs more control. Do not stack multiple RNNoise filters.

## Limits

RNNoise expects 48 kHz, 480-sample frames. Filter resamples other OBS rates and adds block/resampler latency. Exact latency depends on OBS audio rate and resampler buffering. Discord parity is not guaranteed.
