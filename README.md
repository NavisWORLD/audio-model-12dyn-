# CST Audio 12dyn — C++ Foley engine (experimental)

A **dependency-free, offline C++17 audio addon** from Cory Davis / NavisWORLD for generating procedural Foley sound effects. It uses a compact 12-scalar state update derived from the **public** [Beast Box dyn12 reference](https://github.com/NavisWORLD/The-beast-box-/blob/main/beastbox/dyn12.py). It is a working **synthesizer and WAV exporter**, **not** a trained generative audio model, a cloning system, or evidence of lower costs than a measured alternative.

Designed as a practical response to [bone's Foley/SFX question](https://x.com/boneGPT) with no pretrained weights, cloud subscription, training dataset, or GPU required. Create wood, gravel, metal, concrete, fabric and water footsteps from timestamped events; supply your own project data. No copyrighted source recordings are included.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
./build/cst-foley --probe
./build/cst-foley --render --events examples/footsteps.csv --seconds 4.5 --out footsteps.wav
./build/cst-foley --render --events examples/footsteps.csv --seconds 4.5 --static --out control.wav
```

On Windows, run `build\Release\cst-foley.exe` for multi-config Visual Studio builds. Standard-library only: CMake, C++17 compiler and enough disk space for a PCM16 WAV. Samples are rendered offline, not through a microphone or real-time audio driver. `--render` without `--events` makes a short built-in demonstration. `--help` lists options.

## Learn and extend the math

This repo includes a **teach-and-build guide** so you can understand the CST/dyn12-inspired controller and upgrade the Foley engine without guessing at private equations:

1. **[Audio math for humans](docs/MATH_FOR_HUMANS.md)** — pronounce the symbols; learn samples, oscillators, envelopes and the twelve-state update; follow a step-by-step worked example.
2. **[Exact CST/dyn12 audio equations](docs/AUDIO_MATH.md)** — line-to-code map of the public-reference recurrence, four event drives, two active audio taps, all six material formulas, panning, clipping and WAV export.
3. **[Extend the engine](docs/EXTENDING_THE_ENGINE.md)** — add surfaces, try new state routing, build a measured hardware profile, design a real-time engine or add a separately trained predictor while preserving controls and evidence.
4. **[Hardware profiling](docs/PROFILE.md)** — measure rather than assume performance on your own computer.

**Code-versus-research boundary:** all 12 state scalars evolve, but only \`state[1]\` and \`state[4]\` alter the current waveform; no audio neural model is trained here. The math guides document the current implementation and label proposed upgrades separately. Public viewing does not grant third-party commercial or redistribution rights until the owner publishes license terms.

## Personal hardware tuning (not other people's hardware)

`--probe` reads the **machine executing the program**: OS, logical CPU count, and physical RAM when available. Default offline profile: 32 kHz/512 frames if <=2 logical cores or <=2 GiB RAM; 48 kHz/512 frames for 3–4 cores; otherwise 48 kHz/256 frames. Probe reports RAM=0 when unknown. Override defaults with `--sample-rate`, `--block` and `--channels`. Single-threaded CPU DSP; **no GPU kernel or GPU auto-detection** exists yet, so hardware probing is not a GPU optimization claim. Audio sample rates are render settings, not an assertion about your playback device.

A genuine bone-specific profile requires his actual OS, CPU, RAM, audio interface, target sample rate and latency/quality goal. `--probe` prints the beginning of that profile locally without leaking identifiers. Offline resource costs also depend on how many events overlap.

## Event and integration contract

CSV header: `seconds,surface,intensity,pan`. Each event uses a nonnegative time, one of `wood|gravel|metal|concrete|fabric|water`, intensity in [0,1], pan in [-1,1]. A step has a synthetic impact oscillator, surface-specific noise and an exponential envelope. The dyn12 state updates **once per rendering block** using activity, material hardness, pan and transient drive; bounded state alters tone and texture. Use `--static` as an otherwise-matched bypass. Clip protection limits PCM output, and `peak_preclip` reports the pre-limiter peak. Stereo uses equal-power panning; mono is a downmix.

For C++ integration include `<cst_audio/audio.hpp>`, link the `cst_audio` CMake target, and call `render(cfg, seconds, events)`; WAV export is optional. No permissions for files, network or credentials are granted to a model by this library. This code is an **independent auditable audio application of the public reference dynamic**, not a private COSMOS binary or a byte-for-byte reproduction of an undocumented model.

## Evidence & limitations

`ctest` checks the public-reference dyn12 formula on an initial step, deterministic output, an active/static difference, RIFF file length, range rejection and profile selection. These tests establish correctness of the local implementation only. They do **not** establish perceived realism, latency on a target computer, trained audio-model performance, or measured training cost. For genuine cost comparison, benchmark CPU time/memory, audition clips and compare against a documented dataset and quality rubric on **the same machine**. Do not claim that synthetic Foley replaces production-recorded Foley without listening tests.

Source lineage: [The-beast-box-/beastbox/dyn12.py](https://github.com/NavisWORLD/The-beast-box-/blob/main/beastbox/dyn12.py), public reference (0.86 leak, 0.14 drive, 0.015 sinusoidal forcing). Audio signal design and tests are separate new implementations. Do not imply FlyWire, IBM, quantum advantage or biological interpretation in this renderer. See [docs/PROFILE.md](docs/PROFILE.md) for repeatable hardware profiling.

**Reuse rights:** This repository has not yet been given an explicit software license. Public source visibility alone is not a grant to copy, redistribute or commercially reuse it; the owner must choose and publish licensing terms before third-party integration.
